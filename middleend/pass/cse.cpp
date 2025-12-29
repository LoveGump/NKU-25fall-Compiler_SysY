#include <middleend/pass/cse.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/dominfo.h>
#include <middleend/visitor/utils/operand_replace_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/visitor/utils/expr_key_visitor.h>
#include <transfer.h>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace ME
{
    void CSEPass::runOnFunction(Function& function)
    {
        // CSE（Common Subexpression Elimination）：
        // - 跨块 CSE：沿支配树 DFS，在支配路径上维护“表达式 -> 已有值”的映射
        // - 同时做一个轻量的“隐式 CSE”：当条件寄存器值可确定时，将条件分支改写为无条件跳转
        // - 删除被替代的冗余指令，并对剩余指令做操作数替换
        bool changed = runDominatorCSE(function);
        // changed |= runBlockLocalCSE(function);
        if (changed) Analysis::AM.invalidate(function);
    }

    // 对于跨块的cse，需要考虑支配关系和控制流
    // 使用支配树进行深度优先遍历，在遍历过程中维护表达式-值映射表
    bool CSEPass::runDominatorCSE(Function& function)
    {
        Analysis::AM.invalidate(function);
        auto* cfg = Analysis::AM.get<Analysis::CFG>(function);
        auto* dom = Analysis::AM.get<Analysis::DomInfo>(function);
        if (!cfg || !dom || cfg->id2block.empty()) return false;

        bool                                      changed = false;
        const auto&                               domTree = dom->getDomTree();
        // exprMap：在 “当前 DFS 支配路径” 上可用的公共表达式缓存
        // key 由 ExprKeyVisitor 生成（同一个 key 视为等价表达式）
        std::unordered_map<std::string, Operand*> exprMap;

        // eraseSet：记录可以删除的冗余指令（其结果会被已有值替换）
        std::unordered_set<Instruction*> eraseSet;

        // replaceRegs：reg -> operand，表示 “这个寄存器的值等价于另一个值”，用于后续改写操作数
        // 在遍历过程中动态维护
        std::unordered_map<size_t, Operand*> replaceRegs;
        OperandReplaceVisitor                replacer(replaceRegs);

        std::unordered_set<size_t> visited;     // 记录已访问的基本块，防止重复访问
        ExprKeyVisitor             keyVisitor;  // 获取指令的key


        // 隐式CSE：记录已知条件值 (寄存器号 -> true/false)
        // 当跳转到一个新块的时候，如果该块只有一个前驱且该前驱是条件分支，
        // 则可以根据跳转方向推导出条件寄存器的值
        std::unordered_map<size_t, bool> knownConditions;

        std::function<void(size_t)> dfs = [&](size_t blockId) {
            // 沿支配树做 DFS，保证当访问某块时：它的支配者块都已被访问并建立了 exprMap
            if (visited.count(blockId)) return;
            visited.insert(blockId);
            // 获取当前基本块
            Block* block = function.getBlock(blockId);
            if (!block) return;

            // 记录本块内新增映射的的key，以便在离开块时撤销，保证 exprMap 只对支配子树可见
            std::vector<std::string> localStack;
            // 记录本块内新增的已知条件，以便在离开块时撤销（保证条件信息只在可推导的子路径有效）
            std::vector<size_t> localConditions;

            for (auto* inst : block->insts)
            {
                // 先做一次“已知等价寄存器”的替换，避免 key 受旧寄存器干扰
                if (!replaceRegs.empty()) { apply(replacer, *inst); }

                // 隐式CSE：检查条件分支是否使用已知条件
                // 如果当前指令是条件分支指令，且条件寄存器的值已知，则替换为无条件跳转
                if (auto* brCond = dynamic_cast<BrCondInst*>(inst))
                {
                    if (brCond->cond && brCond->cond->getType() == OperandType::REG)
                    {
                        size_t condReg = brCond->cond->getRegNum();
                        auto   it      = knownConditions.find(condReg);
                        if (it != knownConditions.end())
                        {
                            // 条件值已知，替换为常量并转为无条件跳转
                            Operand* target  = it->second ? brCond->trueTar : brCond->falseTar;
                            Operand* skipped = it->second ? brCond->falseTar : brCond->trueTar;
                            auto*    newBr   = new BrUncondInst(target);

                            // 从被跳过的目标块的PHI节点中移除当前块的引用
                            if (skipped && skipped->getType() == OperandType::LABEL)
                            {
                                Block* skippedBlock = function.getBlock(skipped->getLabelNum());
                                if (skippedBlock)
                                {
                                    Operand* curLabel = getLabelOperand(block->blockId);
                                    for (auto* phiInst : skippedBlock->insts)
                                    {
                                        if (phiInst->opcode != Operator::PHI) break;
                                        auto* phi = static_cast<PhiInst*>(phiInst);
                                        phi->incomingVals.erase(curLabel);
                                    }
                                }
                            }

                            // 用新指令替换旧指令
                            eraseSet.insert(inst);
                            block->insts.push_back(newBr);
                            changed = true;
                            continue;
                        }
                    }
                }

                // 为指令生成“表达式键”，只对可消除的指令产生 key（例如纯计算、无副作用指令）
                keyVisitor.result.clear();
                apply(keyVisitor, *inst);
                if (keyVisitor.result.empty()) continue;

                // 只有定义了寄存器结果的指令才有替换意义
                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg == 0) continue;

                // 查询当前支配路径上是否已有等价表达式：
                // - 若存在：当前指令冗余，用已有值替换 defReg，并删除当前指令
                // - 若不存在：将本指令结果加入 exprMap，使其对支配子树可用
                auto found = exprMap.find(keyVisitor.result);
                if (found != exprMap.end())
                {
                    // 更新映射至新的操作数
                    replaceRegs[defReg] = found->second;
                    eraseSet.insert(inst);
                    changed = true;
                    continue;
                }

                // 没有等价表达式，记录下来
                exprMap[keyVisitor.result] = getRegOperand(defReg);
                localStack.push_back(keyVisitor.result); // 记录新增的表达式
            }

            if (blockId < domTree.size())
            {
                // 获取当前块的终结指令，检查是否为条件分支
                BrCondInst* brCond  = nullptr;
                size_t      condReg = 0;
                if (!block->insts.empty())
                {
                    brCond = dynamic_cast<BrCondInst*>(block->insts.back());
                    if (brCond && brCond->cond && brCond->cond->getType() == OperandType::REG)
                    {
                        // 如果是条件分支，记录条件寄存器编号
                        condReg = brCond->cond->getRegNum();
                    }
                    else { brCond = nullptr; }
                }

                for (int child : domTree[blockId])
                {
                    // 遍历孩子节点
                    if (child == static_cast<int>(blockId)) continue;

                    // 隐式CSE：根据分支方向设置已知条件值
                    // 条件：1) 子块是直接分支目标 2) 子块只有一个前驱（当前块）
                    size_t prevSize = localConditions.size(); // 记录当前已知条件数量，便于回溯
                    if (brCond && condReg != 0)
                    {
                        // 如果是条件分支，记录条件寄存器编号
                        size_t trueBlock  = brCond->trueTar->getLabelNum();
                        size_t falseBlock = brCond->falseTar->getLabelNum();
                        size_t childId    = static_cast<size_t>(child);

                        // 检查当前子块是否只有一个前驱，如果只有一个前驱，则可以确定条件值（这里先只考虑这种情况）
                        bool singlePred = childId < cfg->invG_id.size() && cfg->invG_id[childId].size() == 1;

                        if (singlePred && trueBlock != falseBlock)
                        {
                            // 将条件寄存器的值设置为 true/false，仅对“唯一前驱”的子块成立
                            if (childId == trueBlock)
                            {
                                knownConditions[condReg] = true;
                                localConditions.push_back(condReg);
                            }
                            else if (childId == falseBlock)
                            {
                                knownConditions[condReg] = false;
                                localConditions.push_back(condReg);
                            }
                        }
                    }

                    // dfs子节点
                    dfs(static_cast<size_t>(child));

                    // 撤销本次添加的条件
                    while (localConditions.size() > prevSize)
                    {
                        // 如果本次添加了已知条件，弹栈
                        knownConditions.erase(localConditions.back());
                        localConditions.pop_back();
                    }
                }
            }

            // 撤销本块内的表达式映射
            for (const auto& key : localStack) { exprMap.erase(key); }
        };

        dfs(0);

        // 先删冗余指令，避免后续 finalReplacer 遇到悬挂指令指针
        if (!eraseSet.empty())
        {
            // 根据eraseSet，构造新的指令列表
            for (auto& [id, block] : function.blocks)
            {
                std::deque<Instruction*> newInsts;
                for (auto* inst : block->insts)
                {
                    if (eraseSet.count(inst)) { delete inst; }
                    else { newInsts.push_back(inst); }
                }
                block->insts = newInsts;
            }
        }

        // 如果有替换，进行最终替换
        if (!replaceRegs.empty())
        {
            OperandReplaceVisitor finalReplacer(replaceRegs);
            for (auto& [id, block] : function.blocks)
            {
                for (auto* inst : block->insts) { apply(finalReplacer, *inst); }
            }
        }
        return changed;
    }

    // 对于同一块内的cse，不考虑支配关系和控制流
    // 直接将等价表达式替换为已存在的值即可
    bool CSEPass::runBlockLocalCSE(Function& function)
    {
        bool changed = false;

        // 记录每一条指令对应的块，以及使用某个寄存器的指令列表
        std::unordered_map<Instruction*, size_t> inst2block;
        UserCollector                            userCollector;
        for (auto& [id, block] : function.blocks)
        {
            for (auto* inst : block->insts)
            {
                inst2block[inst] = id;
                apply(userCollector, *inst);
            }
        }

        for (auto& [id, block] : function.blocks)
        {
            // 寄存器编号 -> 替换用的操作数
            std::unordered_map<size_t, Operand*> replaceRegs;
            OperandReplaceVisitor                replacer(replaceRegs);

            // 表达式键 -> 已存在的操作数
            std::unordered_map<std::string, Operand*> exprMap;
            ExprKeyVisitor                            keyVisitor;

            // 新建指令列表
            std::deque<Instruction*> newInsts;

            for (auto* inst : block->insts)
            {
                // 尝试进行替换
                if (!replaceRegs.empty()) apply(replacer, *inst);

                // 获取指令对应的key值
                keyVisitor.result.clear();
                apply(keyVisitor, *inst);

                // 非 CSE 候选指令直接加入新指令列表
                if (keyVisitor.result.empty())
                {
                    newInsts.push_back(inst);
                    continue;
                }

                // 获取指令定义的新值的寄存器编号
                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg == 0)
                {
                    // 没有定义值的指令直接加入新指令列表
                    newInsts.push_back(inst);
                    continue;
                }

                // 检查是否已等价表达式存在
                auto found = exprMap.find(keyVisitor.result);
                if (found != exprMap.end())
                {
                    // 如果有等价表达式，检查是否有块外使用
                    bool externalUse = false;
                    auto uit         = userCollector.userMap.find(defReg);
                    if (uit != userCollector.userMap.end())
                    {
                        for (auto* useInst : uit->second)
                        {
                            auto bit = inst2block.find(useInst);
                            if (bit != inst2block.end() && bit->second != id)
                            {
                                // 块外使用了
                                externalUse = true;
                                break;
                            }
                        }
                    }

                    // 更改映射至新的操作数
                    replaceRegs[defReg] = found->second;
                    if (externalUse) { newInsts.push_back(inst); }
                    else
                    {
                        delete inst;
                        changed = true;
                    }
                    continue;
                }

                // 没有等价表达式，记录下来
                exprMap.emplace(std::move(keyVisitor.result), getRegOperand(defReg));
                newInsts.push_back(inst);
            }
            block->insts = newInsts;
        }
        return changed;
    }

}  // namespace ME

/*
CSE 流程总结对应 dominator CSE ：
1) 构建 CFG 与支配树，从入口块(0)沿支配树 DFS 遍历基本块。
2) 维护 exprMap：在当前 DFS 支配路径上记录“表达式 key -> 已有结果 operand”。
3) 逐指令处理：
   - 先用 replaceRegs 做操作数替换，保证 key 计算基于最新等价值；
   - 若遇到条件分支且条件值已知：改写为无条件跳转，并更新被跳过块的 Phi incoming；
   - 为可消除指令生成 key：若 key 已在 exprMap 中出现，记录 defReg 替换并删除该指令；否则将其加入 exprMap。
4) 递归访问支配子块，并在离开块时撤销本块新增的 exprMap/knownConditions 条目（使缓存只对支配子树有效）。
5) 最后统一删除冗余指令，并对函数内剩余指令做一次全量寄存器替换。
*/
