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
        std::unordered_map<std::string, Operand*> exprMap;

        std::unordered_set<Instruction*> eraseSet;

        std::unordered_map<size_t, Operand*> replaceRegs;
        OperandReplaceVisitor                replacer(replaceRegs);

        std::unordered_set<size_t> visited;
        ExprKeyVisitor             keyVisitor;

        // 隐式CSE：记录已知条件值 (寄存器号 -> true/false)
        std::unordered_map<size_t, bool> knownConditions;

        std::function<void(size_t)> dfs = [&](size_t blockId) {
            // 深度优先搜索遍历所有基本块
            if (visited.count(blockId)) return;
            visited.insert(blockId);
            // 获取当前基本块
            Block* block = function.getBlock(blockId);
            if (!block) return;

            // 记录本块内新增的表达式映射，以便在离开块时撤销
            std::vector<std::tuple<std::string, bool, Operand*>> localStack;
            // 记录本块内新增的已知条件，以便在离开块时撤销
            std::vector<size_t> localConditions;

            for (auto* inst : block->insts)
            {
                // 先进行寄存器替换
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

                // 获取指令对应的key值
                keyVisitor.result.clear();
                apply(keyVisitor, *inst);
                if (keyVisitor.result.empty()) continue;

                // 获取指令定义的新值的寄存器编号
                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg == 0) continue;

                // 查询是否已有等价表达式
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
                localStack.emplace_back(keyVisitor.result, false, nullptr);
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
                    if (child == static_cast<int>(blockId)) continue;

                    // 隐式CSE：根据分支方向设置已知条件值
                    // 条件：1) 子块是直接分支目标 2) 子块只有一个前驱（当前块）
                    size_t prevSize = localConditions.size();
                    if (brCond && condReg != 0)
                    {
                        // 如果是条件分支，记录条件寄存器编号
                        size_t trueBlock  = brCond->trueTar->getLabelNum();
                        size_t falseBlock = brCond->falseTar->getLabelNum();
                        size_t childId    = static_cast<size_t>(child);

                        // 检查子块是否只有一个前驱，如果只有一个前驱，则可以确定条件值（这里先只考虑这种情况）
                        bool singlePred = childId < cfg->invG_id.size() && cfg->invG_id[childId].size() == 1;

                        if (singlePred && trueBlock != falseBlock)
                        {
                            // 将条件寄存器的值设置为true，压栈
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
            for (auto it = localStack.rbegin(); it != localStack.rend(); ++it)
            {
                const std::string& key    = std::get<0>(*it);
                bool               hadOld = std::get<1>(*it);
                Operand*           oldVal = std::get<2>(*it);
                if (hadOld)
                {
                    // 将key映射为旧值
                    exprMap[key] = oldVal;
                }
                else { exprMap.erase(key); }
            }
        };

        dfs(0);

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
    // 没有使用
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
