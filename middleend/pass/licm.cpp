#include <middleend/pass/licm.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/loop_info.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/licm_visitor.h>
#include <middleend/visitor/utils/operand_replace_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <deque>



/*
指令在语义上与迭代无关（loop-invariant）

指令的所有操作数（operands）在循环中保持不变（要么是常量，要么来自循环外、或者来自已确认的不变指令）。

指令本身无副作用（no side-effects）

不能是 store、volatile load、atomic、inline asm、I/O，或其它必须保留执行时机的操作。

不会违反内存依赖（memory-safety / alias）//这里直接不考虑

若是 load，必须证明循环内没有可能写入同一地址的 store（需要 Alias Analysis / MemorySSA / AA 信息）；若是有写，不能外提。

不会改变程序可见的异常/陷阱语义

如可能的除零、溢出、对齐异常等，外提不能让异常发生在原本不会发生的位置（或改变发生顺序），除非允许这样的重排序（例如编译器允许把浮点精度/异常语义改变时需有对应的开关）。

支配关系 / 可见性

要把指令放到 loop preheader（或循环外某点），保证该位置在所有原始使用点之前（指令的新位置必须支配其所有 uses 的控制流路径，通常把它放入 preheader 即可）。

循环首部（preheader）存在或可创建

如果没有 preheader，需要先创建一个安全的 preheader 以放置提取指令。

不存在循环依赖（loop-carried dependence）
*/
namespace ME
{
    // 模块级别的 LICM 优化入口
    // 功能：对整个模块进行循环不变量外提优化
    // 实现思路：
    // 1. 首先收集所有不可变的全局变量（用于判断全局变量 load 是否可以外提）
    // 2. 然后对模块中的每个函数分别进行 LICM 优化
    void LICMPass::runOnModule(Module& module)
    {
        collectImmutableGlobals(module);
        for (auto* function : module.functions) runOnFunctionImpl(*function);
    }

    // 函数级别的 LICM 优化入口
    // 功能：对单个函数进行循环不变量外提优化
    // 实现思路：直接调用核心实现函数 runOnFunctionImpl
    void LICMPass::runOnFunction(Function& function) { runOnFunctionImpl(function); }

    // LICM (Loop Invariant Code Motion) 优化的核心函数
    // 对给定函数中的所有循环进行循环不变量外提优化
    void LICMPass::runOnFunctionImpl(Function& function)
    {
        // 步骤 1: 基本检查
        // 如果函数没有定义或没有基本块，直接返回
        if (!function.funcDef) return;
        if (function.blocks.empty()) return;

        // 步骤 2: 获取必要的分析结果
        // cfg: 控制流图 (Control Flow Graph)，用于分析基本块之间的跳转关系
        // dom: 支配信息 (Dominator Info)，用于判断基本块之间的支配关系
        //      支配关系用于确保外提的指令在循环的所有执行路径上都可用
        auto* cfg = Analysis::AM.get<Analysis::CFG>(function);
        auto* dom = Analysis::AM.get<Analysis::DomInfo>(function);
        if (!cfg || !dom) return;
        
        // imm_dom: 直接支配者数组，imm_dom[i] 表示基本块 i 的直接支配者
        // 用于快速判断一个基本块是否支配另一个基本块
        const auto& imm_dom = dom->getImmDom();

        // 步骤 3: 获取循环信息
        // loopInfo: 循环信息分析结果，包含函数中所有检测到的循环
        // 如果函数中没有循环，则无需进行 LICM 优化
        auto* loopInfo = Analysis::AM.get<Analysis::LoopInfo>(function);
        if (!loopInfo || loopInfo->getNumLoops() == 0) return;

        // 步骤 4: 建立 def-use 索引，便于后续判断循环不变量
        // regDefs: 寄存器定义映射，regDefs[regId] = 定义该寄存器的指令
        //          用于快速查找某个寄存器是由哪条指令定义的
        // regDefBlock: 寄存器定义所在的基本块映射，regDefBlock[regId] = 定义该寄存器的基本块 ID
        //              用于判断寄存器定义是否在循环内
        // instBlock: 指令所在的基本块映射，instBlock[inst] = 该指令所在的基本块 ID
        //            用于快速查找指令位于哪个基本块
        // userMap: 寄存器使用映射，userMap[regId] = 使用该寄存器的所有指令列表
        //          用于快速查找哪些指令使用了某个寄存器
        std::unordered_map<size_t, Instruction*>    regDefs;
        std::unordered_map<size_t, size_t>          regDefBlock;
        std::unordered_map<Instruction*, size_t>    instBlock;
        std::map<size_t, std::vector<Instruction*>> userMap;
        buildDefUseMaps(function, regDefs, regDefBlock, instBlock, userMap);
        
        // changed: 标记函数是否被修改，如果进行了优化，需要标记为 true
        //          以便后续使分析结果失效（因为 CFG 可能改变）
        bool changed = false;

        // 步骤 5: 遍历函数中的所有循环，对每个循环进行 LICM 优化
        for (auto& loopPtr : loopInfo->getAllLoops())
        {
            // loop: 当前处理的循环对象，包含循环的所有基本块、头块、回边等信息
            Analysis::Loop& loop = *loopPtr;

            // 步骤 5.1: 收集循环的副作用信息
            // loopStoreGlobals: 循环内所有被写入（store）的全局变量的 operand 指针集合
            //                  用于判断全局变量 load 是否可以在循环内安全外提
            //                  如果全局变量在循环内被写入，则不能外提其 load 操作
            // loopHasCall: 标记循环内是否存在函数调用
            //              如果循环内有函数调用，需要更保守地判断哪些指令可以外提
            //              因为函数调用可能修改全局变量或产生其他副作用
            std::set<Operand*> loopStoreGlobals;
            bool               loopHasCall = false;
            collectLoopEffects(function, loop, loopStoreGlobals, loopHasCall);

            // 步骤 5.2: 检查循环内是否存在内存相关操作
            // restrictHeader: 标记是否限制只提升循环头块中的标量不变量
            //                  如果循环内存在内存操作（load/store）或函数调用，
            //                  为了安全起见，只提升循环头块中的标量不变量
            //                  这样可以避免在条件分支中提前执行可能不安全的操作
            // memoryVisitor: 访问者模式，用于判断指令是否为内存相关操作
            //               包括 load、store、alloca、call 等可能影响内存的指令
            bool                  restrictHeader = false;
            LICMMemoryLikeVisitor memoryVisitor;
            // 遍历循环内的所有基本块，检查是否存在内存相关操作
            for (size_t blockId : loop.blocks)
            {
                Block* block = function.getBlock(blockId);
                if (!block) continue;
                // 遍历基本块内的所有指令
                for (auto* inst : block->insts)
                {
                    if (!inst) continue;
                    // 如果发现内存相关操作，设置 restrictHeader = true
                    if (apply(memoryVisitor, *inst))
                    {
                        restrictHeader = true;
                        break;
                    }
                }
                if (restrictHeader) break;
            }

            // 步骤 5.3: 收集循环中的所有循环不变量指令
            // invariantInsts: 循环不变量指令集合，包含所有可以安全外提到循环外的指令
            //                这些指令在循环的每次迭代中计算结果相同，可以只计算一次
            // invariantRegs: 循环不变量寄存器集合，包含所有循环不变量的寄存器 ID
            //                用于快速判断某个寄存器是否为循环不变量
            std::set<Instruction*> invariantInsts;
            std::set<size_t>       invariantRegs;
            collectInvariantInsts(function,
                loop,                    // 当前循环
                regDefBlock,            // 寄存器定义所在的基本块映射
                userMap,                // 寄存器使用映射
                instBlock,              // 指令所在的基本块映射
                imm_dom,                // 直接支配者数组
                restrictHeader,         // 是否限制只提升头块的标量不变量
                loopStoreGlobals,       // 循环内被写入的全局变量集合
                loopHasCall,            // 循环内是否存在函数调用
                invariantInsts,         // 输出：循环不变量指令集合
                invariantRegs);         // 输出：循环不变量寄存器集合
            
            // 如果没有找到循环不变量，跳过当前循环
            if (invariantInsts.empty()) continue;

            // 步骤 5.4: 创建或获取循环的 preheader（预头块）
            // preheader: 循环的预头块，是插入在循环头块之前的一个基本块
            //            所有外提的循环不变量指令都会被移动到这个基本块中
            //            如果循环没有 preheader，需要创建一个
            // cfgChanged: 标记 CFG 是否被修改（如果创建了新的 preheader，CFG 会改变）
            bool   cfgChanged = false;
            Block* preheader = getOrCreatePreheader(function, cfg, loop, cfgChanged);
            if (!preheader) continue;  // 如果无法创建 preheader，跳过当前循环
            if (cfgChanged) changed = true;  // 如果 CFG 被修改，标记函数已改变

            // 步骤 5.5: 构建外提顺序
            // hoistOrder: 外提顺序列表，按照依赖关系排序的循环不变量指令列表
            //             确保在外提时，被依赖的指令先于依赖它的指令被外提
            //             例如：如果指令 B 使用指令 A 的结果，则 A 必须在 B 之前外提
            std::vector<Instruction*> hoistOrder;
            buildHoistOrder(function, loop, invariantInsts, hoistOrder);
            if (hoistOrder.empty()) continue;  // 如果无法构建外提顺序，跳过

            // 步骤 5.6: 执行外提操作
            // 将循环不变量指令按照 hoistOrder 的顺序移动到 preheader 中
            // 这需要更新指令所在的基本块，并确保支配关系正确
            hoistInstructions(preheader, hoistOrder, instBlock, regDefBlock, function, loop, imm_dom);
            changed = true;  // 标记函数已被修改
        }

        // 步骤 6: 如果函数被修改，使分析结果失效
        // 因为外提操作可能改变了 CFG 或其他分析结果，需要重新计算
        if (changed) Analysis::AM.invalidate(function);
    }

    void LICMPass::collectImmutableGlobals(Module& module)
    {
        // 步骤 1: 初始化 - 清空不可变全局变量集合
        immutableGlobals.clear();

        // 步骤 2: 初始假设 - 将所有全局变量都标记为不可变
        // 这是一个乐观的假设：我们假设所有全局变量在循环执行期间不会被修改
        // 后续步骤会逐步排除那些被写入的全局变量
        for (auto* glb : module.globalVars)
        {
            if (glb) immutableGlobals.insert(glb->name);
        }

        // 步骤 3: 收集所有已定义的函数名
        // 用于后续判断函数调用是内部函数还是外部函数
        // 内部函数：在当前模块中有定义，我们可以分析其行为
        // 外部函数：未定义，可能修改任何全局变量，需要保守处理
        std::unordered_set<std::string> definedFuncs;
        for (auto* function : module.functions)
        {
            if (function && function->funcDef) definedFuncs.insert(function->funcDef->funcName);
        }

        // 步骤 4: 查找所有对全局变量的 store 操作
        // 如果某个全局变量被写入（store），说明它是可变的，不能外提
        // 需要从不可变集合中移除
        LICMGlobalStoreVisitor storeVisitor;
        for (auto* function : module.functions)
        {
            if (!function) continue;
            for (auto& [id, block] : function->blocks)
            {
                for (auto* inst : block->insts)
                {
                    if (!inst) continue;
                    // 检查当前指令是否是对全局变量的 store 操作
                    Operand* globalOp = apply(storeVisitor, *inst);
                    if (!globalOp) continue;  // 不是 store 或不是全局变量
                    if (globalOp->getType() != OperandType::GLOBAL) continue;
                    // 找到被写入的全局变量，从不可变集合中移除
                    auto* g = static_cast<GlobalOperand*>(globalOp);
                    immutableGlobals.erase(g->name);
                    continue;
                }
            }
        }

        // 步骤 5: 检查是否有调用未知外部函数的情况
        // 如果调用了外部函数（未在当前模块中定义），我们无法分析其行为
        // 保守地认为所有全局变量都可能被外部函数修改
        // 因此清空不可变集合，避免错误的外提优化
        for (auto* function : module.functions)
        {
            if (!function) continue;
            for (auto& [id, block] : function->blocks)
            {
                for (auto* inst : block->insts)
                {
                    auto* call = dynamic_cast<CallInst*>(inst);
                    if (!call) continue;  // 不是函数调用指令
                    // 检查调用的函数是否在当前模块中定义
                    if (definedFuncs.find(call->funcName) != definedFuncs.end()) continue;
                    // 调用了外部函数，保守处理：清空所有不可变全局变量
                    // 因为外部函数可能修改任何全局变量
                    immutableGlobals.clear();
                    return;
                }
            }
        }
        // 如果执行到这里，说明：
        // 1. 所有全局变量的 store 操作都已检查完毕
        // 2. 没有调用未知的外部函数
        // immutableGlobals 中剩余的全局变量就是真正不可变的，可以安全外提
    }

    //判断基本块dom是否支配node
    //只有支配所有 latch 的指令才能安全外提，否则可能在某些路径上不执行。
    bool LICMPass::dominates(int dom, int node, const std::vector<int>& imm_dom) const
    {
        if (dom == node) return true;
        if (dom < 0 || node < 0) return false;
        if (static_cast<size_t>(dom) >= imm_dom.size() || static_cast<size_t>(node) >= imm_dom.size()) return false;

        int cur = node;
        while (cur >= 0 && static_cast<size_t>(cur) < imm_dom.size())
        {
            int parent = imm_dom[cur];
            if (parent == dom) return true;
            if (parent == cur) break;
            cur = parent;
        }
        return false;
    }

    // 判断基本块是否支配循环的所有 latch（回边目标块）
    // 功能：检查 blockId 是否支配循环中的所有 latch 块
    // 实现思路：
    // 1. 如果循环没有 latch，返回 true（空循环）
    // 2. 遍历所有 latch，检查 blockId 是否支配每个 latch
    // 3. 只有支配所有 latch 的指令才能安全外提，否则可能在某些路径上不执行
    // 参数：
    //   - blockId: 要检查的基本块 ID
    //   - loop: 循环信息
    //   - imm_dom: 直接支配者数组
    // 返回：如果 blockId 支配所有 latch，返回 true；否则返回 false
    bool LICMPass::dominatesAllLatches(
        size_t blockId, const Analysis::Loop& loop, const std::vector<int>& imm_dom) const
    {
        if (loop.latches.empty()) return true;
        for (size_t latchId : loop.latches)
        {
            if (!dominates(static_cast<int>(blockId), static_cast<int>(latchId), imm_dom)) return false;
        }
        return true;
    }

    // 获取或创建循环的 preheader（预头块）
    // 功能：为循环创建或获取一个 preheader 块，用于放置外提的循环不变量指令
    // 实现思路：
    // 1. 收集循环头块的所有外部前驱（来自循环外的块）
    // 2. 如果只有一个外部前驱且它唯一跳转到循环头，直接复用该块作为 preheader
    // 3. 否则创建新的 preheader 块，并重定向所有外部前驱到 preheader
    // 4. 更新循环头块的 Phi 节点，将来自外部前驱的 incoming 值合并到 preheader
    // 参数：
    //   - function: 当前函数
    //   - cfg: 控制流图
    //   - loop: 循环信息
    //   - cfgChanged: 输出参数，如果创建了新块则设为 true
    // 返回：preheader 块的指针，如果无法创建则返回 nullptr
    Block* LICMPass::getOrCreatePreheader(Function& function, Analysis::CFG* cfg, Analysis::Loop& loop, bool& cfgChanged)
    {
        size_t headerId = loop.header;

        // 检查 headerId 是否越界（防护）
        if (headerId >= cfg->invG_id.size()) return nullptr;

        std::set<size_t> predsOutside;

        // 收集循环头header的所有外部前驱（即来自循环外部的前驱块）
        for (size_t pred : cfg->invG_id[headerId])
        {
            if (!loop.contains(pred)) predsOutside.insert(pred);
        }

        // 没有外部前驱，无法构造 preheader，直接返回
        if (predsOutside.empty()) return nullptr;

        // 如果只有一个外部前驱，并且它唯一地跳到循环头，则直接复用这个前驱作为 preheader
        if (predsOutside.size() == 1)
        {
            size_t predId = *predsOutside.begin();
            if (isSingleSuccToHeader(cfg, predId, headerId))
            {
                Block* preheader = function.getBlock(predId);
                return preheader;
            }
        }

        // 上述条件不满足，新建一个 preheader 块
        Block* preheader = function.createBlock();
        preheader->setComment("licm.preheader");  // 添加注释，用于后续识别
        preheader->insts.push_back(new BrUncondInst(getLabelOperand(headerId)));  // preheader 直接跳到 header

        // 修改所有外部前驱的终结指令，使它们跳到新的 preheader 块
        redirectPredsToPreheader(function, predsOutside, headerId, preheader->blockId);

        // 更新 header 的 phi 节点，把来自外部前驱的 incoming，替换为 preheader
        updateHeaderPhis(function, function.getBlock(headerId), predsOutside, preheader->blockId);

        // 标记 CFG 已经发生变化
        cfgChanged = true;

        return preheader;
    }

    // 重定向前驱块的分支目标到 preheader
    // 功能：将所有外部前驱块中指向循环头的跳转改为指向 preheader
    // 实现思路：
    // 1. 创建循环头和 preheader 的标签操作数
    // 2. 遍历所有外部前驱块，获取其终止指令（分支指令）
    // 3. 使用访问者模式将分支指令中的目标标签从循环头替换为 preheader
    // 参数：
    //   - function: 当前函数
    //   - preds: 外部前驱块的 ID 集合
    //   - headerId: 循环头块的 ID
    //   - preheaderId: preheader 块的 ID
    void LICMPass::redirectPredsToPreheader(
        Function& function, const std::set<size_t>& preds, size_t headerId, size_t preheaderId)
    {
        Operand*                 oldLabel = getLabelOperand(headerId);
        Operand*                 newLabel = getLabelOperand(preheaderId);
        LICMBranchReplaceVisitor visitor;

        for (size_t predId : preds)
        {
            Block* pred = function.getBlock(predId);
            if (!pred || pred->insts.empty()) continue;
            Instruction* termInst = pred->insts.back();
            apply(visitor, *termInst, oldLabel, newLabel);
        }
    }

    // 更新循环头块的 Phi 节点
    // 功能：将循环头块 Phi 节点中来自外部前驱的 incoming 值合并到 preheader
    // 实现思路：
    // 1. 遍历循环头块的所有 Phi 节点
    // 2. 收集每个 Phi 中来自外部前驱的 incoming 值
    // 3. 如果只有一个外部前驱，直接将值作为来自 preheader 的 incoming
    // 4. 如果有多个外部前驱，在 preheader 中创建新 Phi 合并这些值，然后循环头 Phi 接收新 Phi 的结果
    // 5. 将新创建的 Phi 插入到 preheader 的终结指令之前
    // 参数：
    //   - function: 当前函数
    //   - header: 循环头块指针
    //   - predsOutside: 外部前驱块的 ID 集合
    //   - preheaderId: preheader 块的 ID
    void LICMPass::updateHeaderPhis(
        Function& function, Block* header, const std::set<size_t>& predsOutside, size_t preheaderId)
    {
        if (!header) return;
        Operand* newLabel  = getLabelOperand(preheaderId);
        Block*   preheader = function.getBlock(preheaderId);
        if (!preheader) return;

        std::vector<Instruction*> preheaderPhis;
        for (auto* inst : header->insts)
        {
            auto* phi = dynamic_cast<PhiInst*>(inst);
            if (!phi) continue;

            std::vector<std::pair<Operand*, Operand*>> moved;
            for (size_t predId : predsOutside)
            {
                Operand* oldLabel = getLabelOperand(predId);
                auto     it       = phi->incomingVals.find(oldLabel);
                if (it == phi->incomingVals.end()) continue;
                moved.emplace_back(oldLabel, it->second);
            }

            if (moved.empty()) continue;
            for (auto& item : moved) phi->incomingVals.erase(item.first);

            if (predsOutside.size() == 1)
            {
                phi->addIncoming(moved.front().second, newLabel);
                continue;
            }

            Operand* newRes = getRegOperand(function.getNewRegId());
            auto*    newPhi = new PhiInst(phi->dt, newRes);
            for (auto& item : moved) newPhi->addIncoming(item.second, item.first);
            preheaderPhis.push_back(newPhi);
            phi->addIncoming(newRes, newLabel);
        }

        if (preheaderPhis.empty()) return;

        Instruction* terminator = nullptr;
        if (!preheader->insts.empty() && preheader->insts.back()->isTerminator())
        {
            terminator = preheader->insts.back();
            preheader->insts.pop_back();
        }

        for (auto* inst : preheaderPhis) preheader->insts.push_back(inst);
        if (terminator) preheader->insts.push_back(terminator);
    }

    // 判断前驱块是否唯一跳转到循环头
    // 功能：检查 predId 块是否只有一个后继，且该后继是循环头
    // 实现思路：
    // 1. 检查 predId 是否在有效范围内
    // 2. 获取 predId 的所有后继块
    // 3. 如果后继数量为 1 且是循环头，返回 true
    // 用途：用于判断是否可以复用某个前驱块作为 preheader（避免创建新块）
    // 参数：
    //   - cfg: 控制流图
    //   - predId: 前驱块 ID
    //   - headerId: 循环头块 ID
    // 返回：如果前驱块唯一跳转到循环头，返回 true；否则返回 false
    bool LICMPass::isSingleSuccToHeader(Analysis::CFG* cfg, size_t predId, size_t headerId) const
    {
        if (predId >= cfg->G_id.size()) return false;
        const auto& succs = cfg->G_id[predId];
        if (succs.size() != 1) return false;
        return succs.front() == headerId;
    }

    // 构建 def-use 映射关系
    // 功能：建立函数中所有指令的定义-使用关系索引，用于快速查找寄存器定义和使用
    // 实现思路：
    // 1. 遍历函数中所有基本块和指令
    // 2. 记录每条指令所在的基本块（instBlock）
    // 3. 对于每条定义寄存器的指令，记录寄存器到指令的映射（regDefs）和寄存器到基本块的映射（regDefBlock）
    // 4. 使用访问者模式收集所有寄存器的使用情况（userMap）
    // 参数：
    //   - function: 当前函数
    //   - regDefs: 输出参数，寄存器 ID 到定义指令的映射
    //   - regDefBlock: 输出参数，寄存器 ID 到定义所在基本块 ID 的映射
    //   - instBlock: 输出参数，指令到所在基本块 ID 的映射
    //   - userMap: 输出参数，寄存器 ID 到使用该寄存器的指令列表的映射
    void LICMPass::buildDefUseMaps(Function& function, std::unordered_map<size_t, Instruction*>& regDefs,
        std::unordered_map<size_t, size_t>& regDefBlock, std::unordered_map<Instruction*, size_t>& instBlock,
        std::map<size_t, std::vector<Instruction*>>& userMap)
    {
        UserCollector userCollector;

        for (auto& [id, block] : function.blocks)
        {
            for (auto* inst : block->insts)
            {
                instBlock[inst] = id;

                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg != 0)
                {
                    regDefs[defReg]     = inst;
                    regDefBlock[defReg] = id;
                }

                apply(userCollector, *inst);
            }
        }

        userMap = userCollector.userMap;
    }

    // 判断操作数（寄存器）是否为循环不变量
    // 功能：检查寄存器 reg 是否为循环不变量（在循环外定义或已被判定为不变量）
    // 实现思路：
    // 1. 如果寄存器已在循环不变量集合中，直接返回 true
    // 2. 查找寄存器的定义所在基本块
    // 3. 如果定义在循环外，返回 true；否则返回 false
    // 参数：
    //   - reg: 寄存器 ID
    //   - loop: 循环信息
    //   - regDefBlock: 寄存器定义所在基本块的映射
    //   - invariantRegs: 已判定的循环不变量寄存器集合
    // 返回：如果寄存器是循环不变量，返回 true；否则返回 false
    bool LICMPass::isLoopInvariantOperand(size_t reg, const Analysis::Loop& loop,
        const std::unordered_map<size_t, size_t>& regDefBlock, const std::set<size_t>& invariantRegs) const
    {
        if (invariantRegs.find(reg) != invariantRegs.end()) return true;

        auto defIt = regDefBlock.find(reg);
        if (defIt == regDefBlock.end()) return true;
        return !loop.contains(defIt->second);
    }

    //判定变量是否在循环外被使用
    bool LICMPass::areUsesInsideLoop(size_t defReg, const Analysis::Loop& loop,
        const std::map<size_t, std::vector<Instruction*>>& userMap,
        const std::unordered_map<Instruction*, size_t>&    instBlock) const
    {
        auto it = userMap.find(defReg);
        if (it == userMap.end()) return true;

        for (auto* userInst : it->second)
        {
            auto blockIt = instBlock.find(userInst);
            if (blockIt == instBlock.end()) continue;
            // 使用出现在循环外也是允许的，预头块定义能支配后续出口
        }
        return true;
    }

    // 判断指令是否为循环不变量指令
    // 功能：综合判断一条指令是否满足循环不变量的所有条件，可以安全外提
    // 实现思路：
    // 1. 检查是否为安全的全局变量 load（全局变量在循环内未被写入，且可能允许跨函数调用）
    // 2. 检查是否为无副作用的标量指令（算术、比较等）
    // 3. 检查指令定义的所有寄存器是否都在循环内使用（允许在循环外使用，因为 preheader 能支配出口）
    // 4. 检查指令所在基本块是否支配所有 latch（如果不支配，需要额外安全性检查）
    // 5. 检查指令使用的所有操作数是否为循环不变量
    // 参数：
    //   - inst: 要检查的指令
    //   - loop: 循环信息
    //   - regDefBlock: 寄存器定义所在基本块的映射
    //   - invariantRegs: 已判定的循环不变量寄存器集合
    //   - userMap: 寄存器使用映射
    //   - instBlock: 指令所在基本块的映射
    //   - imm_dom: 直接支配者数组
    //   - loopStoreGlobals: 循环内被写入的全局变量集合
    //   - loopHasCall: 循环内是否存在函数调用
    // 返回：如果指令是循环不变量，返回 true；否则返回 false
    bool LICMPass::isInvariantInst(Instruction* inst, const Analysis::Loop& loop,
        const std::unordered_map<size_t, size_t>& regDefBlock, const std::set<size_t>& invariantRegs,
        const std::map<size_t, std::vector<Instruction*>>& userMap,
        const std::unordered_map<Instruction*, size_t>& instBlock, const std::vector<int>& imm_dom,
        const std::set<Operand*>& loopStoreGlobals, bool loopHasCall) const
    {
        if (!inst) return false;

        bool                  isInvariantLoad = false;
        LICMGlobalLoadVisitor loadVisitor;
        Operand*              globalOp = apply(loadVisitor, *inst);
        if (globalOp && loopStoreGlobals.find(globalOp) == loopStoreGlobals.end())
        {
            bool allowAcrossCall = false;
            if (globalOp->getType() == OperandType::GLOBAL)
            {
                const auto* g   = static_cast<const GlobalOperand*>(globalOp);
                allowAcrossCall = immutableGlobals.find(g->name) != immutableGlobals.end();
            }
            if (!loopHasCall || allowAcrossCall) isInvariantLoad = true;
        }

        // 仅外提无副作用的标量指令
        LICMScalarVisitor scalarVisitor;
        if (!isInvariantLoad && !apply(scalarVisitor, *inst)) return false;

        DefCollector defCollector;
        apply(defCollector, *inst);
        size_t defReg = defCollector.getResult();
        if (defReg == 0) return false;
        if (!areUsesInsideLoop(defReg, loop, userMap, instBlock)) { return false; }

        auto blockIt = instBlock.find(inst);
        if (blockIt == instBlock.end()) return false;
        if (!dominatesAllLatches(blockIt->second, loop, imm_dom))
        {
            // 条件块中的指令仅在可安全提前执行时才允许外提
            LICMSafeSpecVisitor safeVisitor;
            if (!apply(safeVisitor, *inst))
            {
                auto* arith = dynamic_cast<ArithmeticInst*>(inst);
                if (!arith) return false;
                if (arith->opcode != Operator::DIV && arith->opcode != Operator::MOD && arith->opcode != Operator::FDIV)
                {
                    return false;
                }
            }
        }

        std::map<size_t, int> uses;
        UseCollector          useCollector(uses);
        apply(useCollector, *inst);

        for (auto& [reg, count] : uses)
        {
            if (!isLoopInvariantOperand(reg, loop, regDefBlock, invariantRegs)) return false;
        }
        return true;
    }

    // 分析当前循环的副作用：记录所有在循环内部被 store（写入）的全局变量，以及循环内是否存在函数调用
    // 这些信息用来辅助判断循环不变量（可能外提的指令）是否安全跨越函数调用，或者会不会影响/被影响全局变量
    // - loopStoreGlobals：循环中所有被写入的全局变量的 operand 指针集合
    // - loopHasCall：循环中是否存在函数调用
    void LICMPass::collectLoopEffects(
        Function& function, const Analysis::Loop& loop, std::set<Operand*>& loopStoreGlobals, bool& loopHasCall) const
    {
        // 初始化，清空之前的副作用信息
        loopStoreGlobals.clear();
        loopHasCall = false;

        LICMGlobalStoreVisitor storeVisitor; // 用于判断指令是否为 store 到全局变量，返回该全局变量 operand
        LICMCallVisitor        callVisitor;  // 用于判断指令是否为函数调用

        // 遍历循环内每个基本块
        for (size_t blockId : loop.blocks)
        {
            Block* block = function.getBlock(blockId);
            if (!block) continue;
            // 遍历基本块内的每一条指令
            for (auto* inst : block->insts)
            {
                if (!inst) continue;
                // 如果当前指令是函数调用，设置 loopHasCall = true
                if (apply(callVisitor, *inst)) loopHasCall = true;
                // 如果当前指令是 store 到全局变量，返回该全局变量 operand
                Operand* globalOp = apply(storeVisitor, *inst);
                if (globalOp) loopStoreGlobals.insert(globalOp);
            }
        }
    }

    // 收集当前循环中的所有可外提（循环不变量）指令
    // 采用迭代的方式持续检查，直到本轮没有新发现的不变量为止（fix-point迭代）
    void LICMPass::collectInvariantInsts(Function& function, const Analysis::Loop& loop,
        const std::unordered_map<size_t, size_t>&          regDefBlock,
        const std::map<size_t, std::vector<Instruction*>>& userMap,
        const std::unordered_map<Instruction*, size_t>& instBlock, const std::vector<int>& imm_dom, bool restrictHeader,
        const std::set<Operand*>& loopStoreGlobals, bool loopHasCall, std::set<Instruction*>& invariantInsts,
        std::set<size_t>& invariantRegs)
    {
        bool changed = true;

        // Fix-point 过程：不断扫描所有循环内的指令，只要有新的不变量被发现就继续下一轮
        while (changed)
        {
            changed = false;
            // 步骤1：遍历循环内所有基本块
            for (size_t blockId : loop.blocks)
            {
                Block* block = function.getBlock(blockId);
                if (!block) continue;
                // 步骤2：扫描每个基本块中的每条指令
                for (auto* inst : block->insts)
                {
                    
                    // 只有能够主导所有 latch 的基本块才允许尝试提前并需要安全性判定
                    if (restrictHeader && blockId != loop.header && !dominatesAllLatches(blockId, loop, imm_dom))
                    {
                        LICMSafeSpecVisitor safeVisitor;
                        // 非安全外提，跳过
                        if (!apply(safeVisitor, *inst)) continue;
                    }
                    // 已经被认为是不变量的指令，跳过，避免多次处理
                    if (invariantInsts.find(inst) != invariantInsts.end()) continue;

                    // 步骤3：判断这条指令是否满足“不变量”条件
                    // 条件包括：所有用到的寄存器来自循环外或者自身已经被判定为不变量，且副作用安全
                    if (!isInvariantInst(inst,
                            loop,
                            regDefBlock,
                            invariantRegs,
                            userMap,
                            instBlock,
                            imm_dom,
                            loopStoreGlobals,
                            loopHasCall))
                        continue;

                    // 步骤4：收集这条指令的定义寄存器
                    DefCollector defCollector;
                    apply(defCollector, *inst);
                    size_t defReg = defCollector.getResult();

                    // 步骤5：将这条指令和其定义的寄存器加入到循环不变量集合
                    invariantInsts.insert(inst);
                    invariantRegs.insert(defReg);

                    // 有新增，本轮还需要继续
                    changed = true;
                }
            }
            // 如果本轮没有新的不变量发现，则while循环退出
        }
        // 最终，invariantInsts/invariantRegs 集合中就是本轮所有可外提指令/寄存器
    }

    // 构建循环不变量指令的外提顺序
    // 功能：根据指令间的依赖关系，对循环不变量指令进行拓扑排序，确定外提顺序
    // 实现思路：
    // 1. 首先按照循环内指令的原始顺序给每条不变量指令分配索引（用于稳定排序）
    // 2. 建立寄存器到定义指令的映射（regToInst）
    // 3. 构建指令依赖图：如果指令 A 使用指令 B 定义的寄存器，则 B -> A 存在依赖边
    // 4. 计算每个指令的入度（依赖它的指令数量）
    // 5. 使用拓扑排序（Kahn 算法）进行排序：每次选择入度为 0 的指令，并更新依赖它的指令的入度
    // 6. 当多个指令入度都为 0 时，按照原始索引顺序选择（保持稳定性）
    // 参数：
    //   - function: 当前函数
    //   - loop: 循环信息
    //   - invariantInsts: 循环不变量指令集合
    //   - hoistOrder: 输出参数，按依赖关系排序的外提顺序列表
    void LICMPass::buildHoistOrder(Function& function, const Analysis::Loop& loop,
        const std::set<Instruction*>& invariantInsts, std::vector<Instruction*>& hoistOrder)
    {
        std::unordered_map<Instruction*, size_t> instIndex;
        size_t                                   index = 0;
        for (size_t blockId : loop.blocks)
        {
            Block* block = function.getBlock(blockId);
            if (!block) continue;
            for (auto* inst : block->insts)
            {
                if (invariantInsts.find(inst) != invariantInsts.end()) instIndex[inst] = index++;
            }
        }

        std::unordered_map<size_t, Instruction*> regToInst;
        for (auto* inst : invariantInsts)
        {
            DefCollector defCollector;
            apply(defCollector, *inst);
            size_t defReg = defCollector.getResult();
            if (defReg != 0) regToInst[defReg] = inst;
        }

        std::unordered_map<Instruction*, std::vector<Instruction*>> edges;
        std::unordered_map<Instruction*, int>                       indegree;
        for (auto* inst : invariantInsts) indegree[inst] = 0;

        for (auto* inst : invariantInsts)
        {
            std::map<size_t, int> uses;
            UseCollector          useCollector(uses);
            apply(useCollector, *inst);

            for (auto& [reg, count] : uses)
            {
                auto defIt = regToInst.find(reg);
                if (defIt == regToInst.end()) continue;
                Instruction* dep = defIt->second;
                edges[dep].push_back(inst);
                indegree[inst] += 1;
            }
        }

        std::set<std::pair<size_t, Instruction*>> ready;
        for (auto& [inst, deg] : indegree)
        {
            if (deg != 0) continue;
            size_t idx = instIndex[inst];
            ready.insert({idx, inst});
        }

        while (!ready.empty())
        {
            auto         it   = ready.begin();
            Instruction* inst = it->second;
            ready.erase(it);
            hoistOrder.push_back(inst);

            for (auto* succ : edges[inst])
            {
                indegree[succ] -= 1;
                if (indegree[succ] == 0)
                {
                    size_t idx = instIndex[succ];
                    ready.insert({idx, succ});
                }
            }
        }
    }

    // 从基本块中移除指令
    // 功能：从指定基本块的指令列表中删除给定的指令
    // 实现思路：
    // 1. 检查基本块和指令指针是否有效
    // 2. 遍历基本块的指令列表，找到目标指令
    // 3. 从列表中删除该指令（只删除第一次出现的）
    // 参数：
    //   - block: 基本块指针
    //   - inst: 要删除的指令指针
    void LICMPass::removeInstFromBlock(Block* block, Instruction* inst)
    {
        if (!block || !inst) return;
        for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
        {
            if (*it != inst) continue;
            block->insts.erase(it);
            return;
        }
    }

    // 执行循环不变量指令的外提操作
    // 功能：将循环不变量指令从循环内移动到 preheader，并处理可能的不安全操作（如除零）
    // 实现思路：
    // 1. 识别需要保护的不安全指令（如除、模、浮点除运算，且不在支配所有 latch 的块中）
    // 2. 将指令分为三类：安全指令（preGuardInsts）、依赖保护指令的指令（postGuardInsts）、不安全指令（unsafeInsts）
    // 3. 从原基本块中移除所有要外提的指令
    // 4. 对于不安全指令，在 preheader 后插入条件分支保护：检查除数是否为零，非零则执行运算，为零则返回 0
    // 5. 将安全指令和依赖保护指令的指令插入到 preheader 的适当位置
    // 6. 更新指令和寄存器定义所在基本块的映射关系
    // 7. 更新循环头块的 Phi 节点，将来自 preheader 的 incoming 改为来自最终的合并块
    // 参数：
    //   - preheader: preheader 基本块指针
    //   - hoistOrder: 按依赖关系排序的外提指令列表
    //   - instBlock: 指令所在基本块的映射（会被更新）
    //   - regDefBlock: 寄存器定义所在基本块的映射（会被更新）
    //   - function: 当前函数
    //   - loop: 循环信息
    //   - imm_dom: 直接支配者数组
    void LICMPass::hoistInstructions(Block* preheader, const std::vector<Instruction*>& hoistOrder,
        std::unordered_map<Instruction*, size_t>& instBlock, std::unordered_map<size_t, size_t>& regDefBlock,
        Function& function, const Analysis::Loop& loop, const std::vector<int>& imm_dom)
    {
        if (!preheader) return;

        std::vector<Instruction*>                unsafeInsts;
        std::unordered_set<Instruction*>         unsafeSet;
        std::set<size_t>                         unsafeRegs;
        std::unordered_map<Instruction*, size_t> defRegs;
        for (auto* inst : hoistOrder)
        {
            DefCollector defCollector;
            apply(defCollector, *inst);
            size_t defReg = defCollector.getResult();
            defRegs[inst] = defReg;

            bool  needsGuard = false;
            auto* arith      = dynamic_cast<ArithmeticInst*>(inst);
            if (arith &&
                (arith->opcode == Operator::DIV || arith->opcode == Operator::MOD || arith->opcode == Operator::FDIV))
            {
                auto blockIt = instBlock.find(inst);
                if (blockIt != instBlock.end() && !dominatesAllLatches(blockIt->second, loop, imm_dom))
                {
                    LICMSafeSpecVisitor safeVisitor;
                    if (!apply(safeVisitor, *inst)) needsGuard = true;
                }
            }

            if (needsGuard)
            {
                unsafeInsts.push_back(inst);
                unsafeSet.insert(inst);
                if (defReg != 0) unsafeRegs.insert(defReg);
            }
        }

        std::vector<Instruction*> preGuardInsts;
        std::vector<Instruction*> postGuardInsts;
        std::set<size_t>          guardedRegs = unsafeRegs;
        for (auto* inst : hoistOrder)
        {
            if (unsafeSet.find(inst) != unsafeSet.end()) continue;

            std::map<size_t, int> uses;
            UseCollector          useCollector(uses);
            apply(useCollector, *inst);

            bool dependsOnGuard = false;
            for (auto& [reg, count] : uses)
            {
                if (guardedRegs.find(reg) != guardedRegs.end())
                {
                    dependsOnGuard = true;
                    break;
                }
            }

            if (dependsOnGuard)
            {
                postGuardInsts.push_back(inst);
                size_t defReg = defRegs[inst];
                if (defReg != 0) guardedRegs.insert(defReg);
            }
            else { preGuardInsts.push_back(inst); }
        }

        // 先从原块中移除，避免重复引用
        for (auto* inst : hoistOrder)
        {
            auto it = instBlock.find(inst);
            if (it == instBlock.end()) continue;
            Block* fromBlock = function.getBlock(it->second);
            removeInstFromBlock(fromBlock, inst);
            instBlock[inst] = preheader->blockId;
            size_t defReg   = defRegs[inst];
            if (defReg != 0) regDefBlock[defReg] = preheader->blockId;
        }

        // 将指令插入到 preheader 的终结指令之前
        Instruction* terminator  = nullptr;
        Operand*     headerLabel = getLabelOperand(loop.header);
        if (!preheader->insts.empty() && preheader->insts.back()->isTerminator())
        {
            terminator = preheader->insts.back();
            preheader->insts.pop_back();

            auto* br = dynamic_cast<BrUncondInst*>(terminator);
            if (br) headerLabel = br->target;
        }

        for (auto* inst : preGuardInsts) preheader->insts.push_back(inst);
        if (unsafeInsts.empty())
        {
            for (auto* inst : postGuardInsts) preheader->insts.push_back(inst);
            if (terminator) preheader->insts.push_back(terminator);
            return;
        }
        if (terminator) delete terminator;

        // 带条件守卫的外提：遇到可能除零的运算时，在 preheader 后插入分支保护
        Block*                                   current = preheader;
        std::unordered_map<size_t, Operand*>     replaceRegs;
        std::unordered_map<Instruction*, size_t> finalBlock;
        for (auto* inst : preGuardInsts) finalBlock[inst] = preheader->blockId;

        for (auto* inst : unsafeInsts)
        {
            auto* arith = dynamic_cast<ArithmeticInst*>(inst);
            if (!arith) continue;

            Operand* oldRes = arith->res;
            Operand* divRes = getRegOperand(function.getNewRegId());
            arith->res      = divRes;

            Operand* zero = nullptr;
            if (arith->dt == DataType::F32)
                zero = getImmeF32Operand(0.0f);
            else
                zero = getImmeI32Operand(0);

            Operand*     cmpRes  = getRegOperand(function.getNewRegId());
            Instruction* cmpInst = nullptr;
            if (arith->dt == DataType::F32)
            {
                cmpInst = new FcmpInst(DataType::F32, FCmpOp::ONE, arith->rhs, zero, cmpRes);
            }
            else { cmpInst = new IcmpInst(DataType::I32, ICmpOp::NE, arith->rhs, zero, cmpRes); }

            Block* thenBlock  = function.createBlock();
            Block* elseBlock  = function.createBlock();
            Block* mergeBlock = function.createBlock();

            current->insts.push_back(cmpInst);
            current->insts.push_back(
                new BrCondInst(cmpRes, getLabelOperand(thenBlock->blockId), getLabelOperand(elseBlock->blockId)));

            thenBlock->insts.push_back(inst);
            thenBlock->insts.push_back(new BrUncondInst(getLabelOperand(mergeBlock->blockId)));

            elseBlock->insts.push_back(new BrUncondInst(getLabelOperand(mergeBlock->blockId)));

            Operand* phiRes = getRegOperand(function.getNewRegId());
            auto*    phi    = new PhiInst(arith->dt, phiRes);
            phi->addIncoming(divRes, getLabelOperand(thenBlock->blockId));
            phi->addIncoming(zero, getLabelOperand(elseBlock->blockId));
            mergeBlock->insts.push_back(phi);

            if (oldRes && oldRes->getType() == OperandType::REG) { replaceRegs[oldRes->getRegNum()] = phiRes; }

            finalBlock[inst] = thenBlock->blockId;
            current          = mergeBlock;
        }

        for (auto* inst : postGuardInsts)
        {
            current->insts.push_back(inst);
            finalBlock[inst] = current->blockId;
        }
        current->insts.push_back(new BrUncondInst(headerLabel));

        if (!replaceRegs.empty())
        {
            OperandReplaceVisitor replaceVisitor(replaceRegs);
            for (size_t blockId : loop.blocks)
            {
                Block* block = function.getBlock(blockId);
                if (!block) continue;
                for (auto* inst : block->insts) { apply(replaceVisitor, *inst); }
            }
            for (auto* inst : postGuardInsts) { apply(replaceVisitor, *inst); }
        }

        Operand*              oldLabel = getLabelOperand(preheader->blockId);
        Operand*              newLabel = getLabelOperand(current->blockId);
        LICMPhiReplaceVisitor phiReplace;
        Block*                header = function.getBlock(loop.header);
        if (header && newLabel != oldLabel)
        {
            for (auto* inst : header->insts) { apply(phiReplace, *inst, oldLabel, newLabel); }
        }

        for (auto& [inst, blockId] : finalBlock)
        {
            instBlock[inst] = blockId;
            size_t defReg   = defRegs[inst];
            if (defReg != 0) regDefBlock[defReg] = blockId;
        }
    }

}  // namespace ME
