#include <middleend/pass/inline.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/clone_visitor.h>
#include <middleend/visitor/utils/rename_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace ME
{
    // 通过调用图检测递归，递归函数不内联
    bool InlinePass::isRecursive(Function& target, Module& module)
    {
        // 没有函数体时直接返回，避免空指针
        if (!target.funcDef) return false;

        // 收集模块内的函数定义，建立名称到函数的映射
        std::map<std::string, Function*> funcMap;
        for (auto* func : module.functions)
        {
            if (func && func->funcDef) funcMap[func->funcDef->funcName] = func;
        }

        // 构建调用图的边集合，记录谁调用了谁
        std::map<std::string, std::set<std::string>> edges;
        for (const auto& entry : funcMap)
        {
            Function* func = entry.second;
            for (const auto& blockEntry : func->blocks)
            {
                Block* block = blockEntry.second;
                if (!block) continue;
                for (auto* inst : block->insts)
                {
                    if (inst->opcode != Operator::CALL) continue;
                    // 发现调用指令时，记录当前函数到被调用函数的边
                    auto* call = static_cast<CallInst*>(inst);
                    if (funcMap.count(call->funcName)) edges[entry.first].insert(call->funcName);
                }
            }
        }

        // DFS 使用的目标名和访问集合
        const std::string     targetName = target.funcDef->funcName;
        std::set<std::string> visited;
        bool                  recursive = false;

        // 深度优先遍历调用图，检查是否回到自身
        std::function<void(const std::string&)> dfs = [&](const std::string& name) {
            if (recursive || visited.count(name)) return;
            // 标记已访问，避免重复遍历导致死循环
            visited.insert(name);
            // 取出当前节点的所有出边
            auto it = edges.find(name);
            if (it == edges.end()) return;
            // 遍历当前函数调用的所有目标
            for (const auto& next : it->second)
            {
                // 如果回到目标函数，说明存在递归
                if (next == targetName)
                {
                    recursive = true;
                    return;
                }
                // 继续向下查找调用链
                dfs(next);
                // 发现递归后提前结束
                if (recursive) return;
            }
        };

        // 从目标函数出发进行遍历
        dfs(targetName);
        return recursive;
    }

    Function* InlinePass::findCallee(Module& module, const std::string& name)
    {
        // 遍历模块内函数表，匹配名称找到被调函数
        for (auto* func : module.functions)
        {
            if (func && func->funcDef && func->funcDef->funcName == name) return func;
        }
        return nullptr;
    }

    Operand* InlinePass::mapOperand(Operand* op, const std::map<size_t, Operand*>& operandMap)
    {
        // 非寄存器操作数无需重映射
        if (!op || op->getType() != OperandType::REG) return op;
        // 若映射表里有替换项则使用替换值
        auto it = operandMap.find(op->getRegNum());
        if (it == operandMap.end()) return op;
        return it->second;
    }

    Instruction* InlinePass::cloneInstruction(const Instruction* inst)
    {
        // 使用访问者克隆指令，统一构造新对象
        if (!inst) return nullptr;
        InstCloner cloner;
        return apply(cloner, const_cast<Instruction&>(*inst));
    }

    void InlinePass::remapLabels(Instruction* inst, const std::map<size_t, size_t>& labelMap)
    {
        // 分支与 PHI 需要对标签进行重映射
        if (inst->opcode == Operator::BR_UNCOND)
        {
            auto* br = static_cast<BrUncondInst*>(inst);
            // 无条件跳转只需要替换一个标签
            if (br->target && br->target->getType() == OperandType::LABEL)
            {
                auto* lab = static_cast<LabelOperand*>(br->target);
                auto  it  = labelMap.find(lab->lnum);
                if (it != labelMap.end()) br->target = getLabelOperand(it->second);
            }
            return;
        }

        if (inst->opcode == Operator::BR_COND)
        {
            // 条件跳转的真假分支标签都需要替换
            auto* br = static_cast<BrCondInst*>(inst);
            if (br->trueTar && br->trueTar->getType() == OperandType::LABEL)
            {
                auto* lab = static_cast<LabelOperand*>(br->trueTar);
                auto  it  = labelMap.find(lab->lnum);
                if (it != labelMap.end()) br->trueTar = getLabelOperand(it->second);
            }
            if (br->falseTar && br->falseTar->getType() == OperandType::LABEL)
            {
                auto* lab = static_cast<LabelOperand*>(br->falseTar);
                auto  it  = labelMap.find(lab->lnum);
                if (it != labelMap.end()) br->falseTar = getLabelOperand(it->second);
            }
            return;
        }

        if (inst->opcode == Operator::PHI)
        {
            // PHI 的 incoming 标签也要同步更新
            auto*                        phi = static_cast<PhiInst*>(inst);
            std::map<Operand*, Operand*> newIncoming;
            // 遍历原有的 incoming，逐个替换标签
            for (auto& [label, val] : phi->incomingVals)
            {
                if (label && label->getType() == OperandType::LABEL)
                {
                    auto* lab = static_cast<LabelOperand*>(label);
                    auto  it  = labelMap.find(lab->lnum);
                    if (it != labelMap.end())
                    {
                        newIncoming[getLabelOperand(it->second)] = val;
                        continue;
                    }
                }
                newIncoming[label] = val;
            }
            phi->incomingVals = std::move(newIncoming);
        }
    }

    // 将 phi 中来自旧标签的边替换为新标签
    void InlinePass::replacePhiIncoming(Block* block, size_t oldLabel, size_t newLabel)
    {
        // 查找指定块中的 PHI，并替换来自旧块的输入边
        if (!block) return;
        Operand* oldOp = getLabelOperand(oldLabel);
        Operand* newOp = getLabelOperand(newLabel);

        for (auto* inst : block->insts)
        {
            // PHI 只出现在块起始处，遇到非 PHI 可结束
            if (inst->opcode != Operator::PHI) break;
            auto* phi = static_cast<PhiInst*>(inst);
            auto  it  = phi->incomingVals.find(oldOp);
            if (it != phi->incomingVals.end())
            {
                Operand* val = it->second;
                phi->incomingVals.erase(it);
                phi->incomingVals[newOp] = val;
            }
        }
    }

    // 选择调用者入口块，用于提升 alloca
    Block* InlinePass::findEntryBlock(Function& func)
    {
        // 优先选择 id 为 0 的块作为入口
        if (func.blocks.empty()) return nullptr;
        auto it0 = func.blocks.find(0);
        if (it0 != func.blocks.end()) return it0->second;

        // 如果没有 0 号块，则根据注释或最小 id 选择入口
        Block* best   = nullptr;
        size_t bestId = 0;
        for (const auto& entry : func.blocks)
        {
            if (!entry.second) continue;
            // 优先选择带有 entry 注释的块
            if (entry.second->comment.find(".entry") != std::string::npos) return entry.second;
            if (!best || entry.first < bestId)
            {
                best   = entry.second;
                bestId = entry.first;
            }
        }
        return best;
    }

    // 根据参数与使用/定义情况生成寄存器映射
    std::map<size_t, Operand*> InlinePass::buildOperandMap(Function& caller, Function& callee, CallInst* callInst)
    {
        // 先建立形参与实参的寄存器映射
        std::map<size_t, Operand*> operandMap;

        auto&  calleeArgs = callee.funcDef->argRegs;
        size_t argCount   = std::min(calleeArgs.size(), callInst->args.size());
        for (size_t i = 0; i < argCount; ++i)
        {
            Operand* argReg = calleeArgs[i].second;
            if (argReg && argReg->getType() == OperandType::REG)
            {
                operandMap[argReg->getRegNum()] = callInst->args[i].second;
            }
        }

        // 收集使用与定义的寄存器编号，保证映射完整
        std::map<size_t, int> useCounts;
        UseCollector          useCollector(useCounts);
        std::set<size_t>      regs;

        for (const auto& entry : callee.blocks)
        {
            Block* block = entry.second;
            // 遍历块内指令收集 use/def
            for (auto* inst : block->insts)
            {
                apply(useCollector, *inst);

                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg != 0) regs.insert(defReg);
            }
        }

        // 需要映射的寄存器包括使用到的所有寄存器
        for (const auto& entry : useCounts) { regs.insert(entry.first); }

        for (auto reg : regs)
        {
            if (operandMap.find(reg) == operandMap.end())
            {
                // 对未出现的寄存器分配新的编号
                operandMap[reg] = getRegOperand(caller.getNewRegId());
            }
        }

        return operandMap;
    }

    // 将被调用函数内联到当前调用点
    bool InlinePass::inlineCall(
        Function& caller, Block* callBlock, size_t callIndex, CallInst* callInst, Function& callee)
    {
        // 先做各类快速检查，确保内联前提满足
        if (!callBlock || !callInst) return false;
        if (callee.blocks.empty()) return false;
        if (callee.funcDef == nullptr) return false;
        if (callee.funcDef->argRegs.size() != callInst->args.size()) return false;
        if (callee.funcDef->retType != callInst->retType) return false;

        // 如果调用有返回值，则要求被调函数所有 return 都有值
        if (callInst->res != nullptr && callInst->retType != DataType::VOID)
        {
            // 检查所有 return 均返回值，避免类型不匹配
            for (const auto& entry : callee.blocks)
            {
                Block* block = entry.second;
                for (auto* inst : block->insts)
                {
                    if (inst->opcode != Operator::RET) continue;
                    if (static_cast<RetInst*>(inst)->res == nullptr) return false;
                }
            }
        }

        // 确认 call 在当前块的位置，避免索引错位
        auto& insts = callBlock->insts;
        if (callIndex >= insts.size()) return false;

        auto iter = insts.begin();
        std::advance(iter, callIndex);
        // 索引与实际指令不符时直接退出
        if (*iter != callInst) return false;

        // 创建 call 后的续接块，用于承接原先的后续指令
        Block* afterCall = caller.createBlock();
        afterCall->setComment("after_inline_" + callee.funcDef->funcName);

        // 拆分调用块：call 前留在原块，call 后移动到 afterCall
        std::deque<Instruction*> newInsts;
        newInsts.insert(newInsts.end(), insts.begin(), iter);
        // call 之后的指令移动到 afterCall
        afterCall->insts.insert(afterCall->insts.end(), std::next(iter), insts.end());
        insts.swap(newInsts);

        // 若 afterCall 末尾有分支，需要调整 PHI 的前驱标签
        if (!afterCall->insts.empty())
        {
            // 读取 afterCall 的终结指令以更新 PHI
            Instruction* term = afterCall->insts.back();
            if (term->opcode == Operator::BR_UNCOND)
            {
                auto* br = static_cast<BrUncondInst*>(term);
                if (br->target && br->target->getType() == OperandType::LABEL)
                {
                    auto* lab = static_cast<LabelOperand*>(br->target);
                    replacePhiIncoming(caller.getBlock(lab->lnum), callBlock->blockId, afterCall->blockId);
                }
            }
            else if (term->opcode == Operator::BR_COND)
            {
                auto* br = static_cast<BrCondInst*>(term);
                // 条件跳转的两个后继都可能有 PHI
                if (br->trueTar && br->trueTar->getType() == OperandType::LABEL)
                {
                    auto* lab = static_cast<LabelOperand*>(br->trueTar);
                    replacePhiIncoming(caller.getBlock(lab->lnum), callBlock->blockId, afterCall->blockId);
                }
                if (br->falseTar && br->falseTar->getType() == OperandType::LABEL)
                {
                    auto* lab = static_cast<LabelOperand*>(br->falseTar);
                    replacePhiIncoming(caller.getBlock(lab->lnum), callBlock->blockId, afterCall->blockId);
                }
            }
        }

        // 为被内联函数的每个块创建新块
        std::map<size_t, Block*> blockMap;
        for (const auto& entry : callee.blocks)
        {
            // 为每个被调块新建对应的内联块
            size_t id       = entry.first;
            Block* newBlock = caller.createBlock();
            newBlock->setComment("inline_" + callee.funcDef->funcName + "_b" + std::to_string(id));
            blockMap[id] = newBlock;
        }

        // 建立旧标签到新标签的映射，方便后续跳转修正
        std::map<size_t, size_t> labelMap;
        for (auto& [id, block] : blockMap) { labelMap[id] = block->blockId; }

        // 找到被调函数入口块并在调用块末尾跳转过去
        size_t entryId = 0;
        if (callee.blocks.find(entryId) == callee.blocks.end()) entryId = callee.blocks.begin()->first;
        Block* entryBlock = blockMap[entryId];
        // 将调用块指向被调入口，形成新的控制流
        callBlock->insertBack(new BrUncondInst(getLabelOperand(entryBlock->blockId)));

        // 为内联准备寄存器映射与操作数重命名器
        auto          operandMap = buildOperandMap(caller, callee, callInst);
        OperandRename renamer;

        // 返回值使用 phi 汇聚
        PhiInst* retPhi = nullptr;
        if (callInst->res != nullptr && callInst->retType != DataType::VOID)
        {
            // 使用 phi 收集所有 return 的返回值
            retPhi = new PhiInst(callInst->retType, callInst->res);
            afterCall->insts.push_front(retPhi);
        }

        // 为收集 alloca 做准备，避免在循环内反复分配
        Operand* afterLabel = getLabelOperand(afterCall->blockId);
        // 收集并提升 alloca，避免出现在循环内导致栈溢出
        Block*                    entryBlockForAllocas = findEntryBlock(caller);
        std::vector<Instruction*> hoistedAllocas;

        for (const auto& entry : callee.blocks)
        {
            size_t id       = entry.first;
            Block* block    = entry.second;
            Block* newBlock = blockMap[id];
            // 遍历被调函数块内的指令并逐条克隆
            for (auto* inst : block->insts)
            {
                if (inst->opcode == Operator::RET)
                {
                    auto* ret = static_cast<RetInst*>(inst);
                    if (retPhi && ret->res != nullptr)
                    {
                        // 将返回值加入到 phi 的 incoming 列表
                        Operand* retVal = mapOperand(ret->res, operandMap);
                        retPhi->addIncoming(retVal, getLabelOperand(newBlock->blockId));
                    }
                    // return 被替换为跳转到 afterCall
                    newBlock->insertBack(new BrUncondInst(afterLabel));
                    continue;
                }

                // 普通指令进行克隆、重命名与标签修正
                Instruction* cloned = cloneInstruction(inst);
                if (!cloned) continue;
                // 重命名寄存器并修复标签引用
                apply(renamer, *cloned, operandMap);
                remapLabels(cloned, labelMap);
                if (cloned->opcode == Operator::ALLOCA && entryBlockForAllocas)
                {
                    // 将 alloca 提升到入口块，避免循环内反复分配导致栈溢出
                    hoistedAllocas.push_back(cloned);
                }
                else { newBlock->insertBack(cloned); }
            }
        }

        // 统一将提升的 alloca 插入到入口块的 PHI/ALLOCA 之后
        if (entryBlockForAllocas && !hoistedAllocas.empty())
        {
            // 插入位置选择在 PHI 与已有 ALLOCA 之后
            auto it = entryBlockForAllocas->insts.begin();
            while (it != entryBlockForAllocas->insts.end() &&
                   ((*it)->opcode == Operator::PHI || (*it)->opcode == Operator::ALLOCA))
            {
                ++it;
            }
            entryBlockForAllocas->insts.insert(it, hoistedAllocas.begin(), hoistedAllocas.end());
        }

        return true;
    }
    void InlinePass::runOnModule(Module& module)
    {
        // 记录模块指针并逐个函数执行内联
        module_ = &module;
        InlineStrategy strategy;
        strategy.analyze(module);

        bool      changed       = true;
        int       iterations    = 0;
        const int maxIterations = 5;

        // 多轮迭代：每轮做一次策略评估，避免信息过期
        while (changed && iterations < maxIterations)
        {
            changed = false;
            iterations++;

            auto order = strategy.getProcessingOrder();
            if (order.empty())
            {
                for (auto* function : module.functions) order.push_back(function);
            }

            for (auto* function : order)
            {
                if (!function) continue;
                if (inlineWithStrategy(*function, strategy)) { changed = true; }
            }

            if (changed) strategy.analyze(module);
        }
        module_ = nullptr;
    }

    void InlinePass::runOnFunction(Function& function)
    {
        // 无模块上下文时直接返回
        if (!module_) return;

        InlineStrategy strategy;
        strategy.analyze(*module_);
        inlineWithStrategy(function, strategy);
    }

    bool InlinePass::inlineWithStrategy(Function& function, InlineStrategy& strategy)
    {
        if (!module_) return false;
        bool                   changed = false;
        std::vector<CallInst*> callsToInline;

        // 先收集待内联的 call，避免遍历过程中修改容器
        for (auto& entry : function.blocks)
        {
            Block* block = entry.second;
            if (!block) continue;

            for (auto* inst : block->insts)
            {
                if (!inst || inst->opcode != Operator::CALL) continue;
                auto*     callInst = static_cast<CallInst*>(inst);
                Function* callee   = findCallee(*module_, callInst->funcName);
                if (!callee) continue;
                if (!strategy.shouldInline(function, *callee, *callInst)) continue;
                callsToInline.push_back(callInst);
            }
        }

        for (auto* callInst : callsToInline)
        {
            Block* block = nullptr;
            size_t index = 0;
            if (!findCallLocation(function, callInst, block, index)) continue;
            Function* callee = findCallee(*module_, callInst->funcName);
            if (!callee) continue;

            if (inlineCall(function, block, index, callInst, *callee)) changed = true;
        }

        if (changed) Analysis::AM.invalidate(function);
        return changed;
    }

    bool InlinePass::findCallLocation(Function& function, CallInst* callInst, Block*& block, size_t& index)
    {
        if (!callInst) return false;
        for (auto& entry : function.blocks)
        {
            Block* cur = entry.second;
            if (!cur) continue;
            for (size_t idx = 0; idx < cur->insts.size(); ++idx)
            {
                if (cur->insts[idx] == callInst)
                {
                    block = cur;
                    index = idx;
                    return true;
                }
            }
        }
        return false;
    }
}  // namespace ME
