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

        std::unordered_set<Instruction*>          eraseSet;

        std::unordered_map<size_t, Operand*>      replaceRegs;
        OperandReplaceVisitor                     replacer(replaceRegs);

        std::unordered_set<size_t>                visited;
        ExprKeyVisitor                            keyVisitor;

        std::function<void(size_t)> dfs = [&](size_t blockId) {
            // 深度优先搜索遍历所有基本块
            if (visited.count(blockId)) return;
            visited.insert(blockId);
            // 获取当前基本块
            Block* block = function.getBlock(blockId);
            if (!block) return;

            // 记录本块内新增的表达式映射，以便在离开块时撤销
            std::vector<std::tuple<std::string, bool, Operand*>> localStack;

            for (auto* inst : block->insts)
            {
                if (!replaceRegs.empty()) {
                    apply(replacer, *inst);
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
                for (int child : domTree[blockId])
                {
                    // 对支配子节点进行DFS
                    if (child == static_cast<int>(blockId)) continue;
                    dfs(static_cast<size_t>(child));
                }
            }

            // 撤销本块内的表达式映射
            for (auto it = localStack.rbegin(); it != localStack.rend(); ++it)
            {
                const std::string& key    = std::get<0>(*it);
                bool               hadOld = std::get<1>(*it);
                Operand*           oldVal = std::get<2>(*it);
                if (hadOld){
                    // 将key映射为旧值
                    exprMap[key] = oldVal;
                }
                else{
                    exprMap.erase(key);
                }
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
                    if (eraseSet.count(inst)) {
                        delete inst;
                    }
                    else{
                        newInsts.push_back(inst);
                    }
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
                for (auto* inst : block->insts) {
                    apply(finalReplacer, *inst);
                }
            }
        }
        return changed;
    }


    // 对于同一块内的cse，不考虑支配关系和控制流
    // 直接将等价表达式替换为已存在的值即可
    // 没有使用
    bool CSEPass::runBlockLocalCSE(Function& function)
    {
        bool  changed = false;

        // 记录每一条指令对应的块，以及使用某个寄存器的指令列表
        std::unordered_map<Instruction*, size_t> inst2block;
        UserCollector  userCollector;
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
            std::unordered_map<size_t, Operand*>      replaceRegs;
            OperandReplaceVisitor                     replacer(replaceRegs);

            // 表达式键 -> 已存在的操作数
            std::unordered_map<std::string, Operand*> exprMap;
            ExprKeyVisitor                            keyVisitor;

            // 新建指令列表
            std::deque<Instruction*>                  newInsts;

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
                    if (externalUse){
                        newInsts.push_back(inst);
                    }
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
