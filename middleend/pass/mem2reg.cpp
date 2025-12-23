#include <middleend/pass/mem2reg.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/rename_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/dominfo.h>
#include <algorithm>
#include <set>
#include <vector>
#include <stack>
#include <map>
#include <queue>
#include <functional>

namespace ME
{
    void Mem2RegPass::runOnFunction(Function& function) { promoteMemoryToRegister(function); }

    bool Mem2RegPass::promoteMemoryToRegister(Function& function)
    {
        auto* domInfo = Analysis::AM.get<Analysis::DomInfo>(function);
        if (!domInfo) return false;

        // 1. 收集所有指令的使用情况
        UserCollector collector;
        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts) { apply(collector, *inst); }
        }

        // 2. 识别可提升的 Alloca 指令
        std::vector<AllocaInst*> allocas;
        std::map<size_t, int>    regToAllocaIdx;  // RegNum -> Index in allocas
        std::set<Instruction*>   toRemove;

        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts)
            {
                if (inst->opcode == Operator::ALLOCA)
                {
                    AllocaInst* alloca = static_cast<AllocaInst*>(inst);
                    size_t      regNum = alloca->res->getRegNum();

                    if (collector.userMap.find(regNum) == collector.userMap.end())
                    {
                        // 无使用的 Alloca，标记删除
                        toRemove.insert(alloca);
                        continue;
                    }

                    bool  promotable = true;
                    auto& users      = collector.userMap[regNum];
                    for (auto user : users)
                    {
                        if (user->opcode == Operator::LOAD)
                        {
                            LoadInst* load = static_cast<LoadInst*>(user);
                            // 必须作为指针操作数使用
                            if (load->ptr->getRegNum() != regNum)
                            {
                                promotable = false;
                                break;
                            }
                        }
                        else if (user->opcode == Operator::STORE)
                        {
                            StoreInst* store = static_cast<StoreInst*>(user);
                            // 必须作为指针操作数使用，不能作为存储的值
                            if (store->ptr->getType() != OperandType::REG ||
                                static_cast<RegOperand*>(store->ptr)->regNum != regNum)
                            {
                                promotable = false;
                                break;
                            }
                        }
                        else
                        {
                            // 其他指令使用（如 GEP, Call 等）暂不支持提升
                            promotable = false;
                            break;
                        }
                    }

                    if (promotable)
                    {
                        regToAllocaIdx[regNum] = allocas.size();
                        allocas.push_back(alloca);
                    }
                }
            }
        }

        if (allocas.empty() && toRemove.empty()) return false;

        // 3. 找到每个 Alloca 的定义块
        std::vector<std::set<int>> defBlocks(allocas.size());
        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts)
            {
                if (inst->opcode == Operator::STORE)
                {
                    StoreInst* store = static_cast<StoreInst*>(inst);
                    if (store->ptr->getType() == OperandType::REG)
                    {
                        size_t reg = static_cast<RegOperand*>(store->ptr)->regNum;
                        if (regToAllocaIdx.count(reg)) { defBlocks[regToAllocaIdx[reg]].insert((int)id); }
                    }
                }
            }
        }

        // 4. 插入 Phi 节点
        const auto& DF = domInfo->getDomFrontier();
        // BlockID -> (AllocaIdx -> PhiInst*)
        std::map<int, std::map<int, PhiInst*>> blockPhis;

        for (size_t i = 0; i < allocas.size(); ++i)
        {
            std::set<int>    F;                                            // 已插入 Phi 的块
            std::vector<int> W(defBlocks[i].begin(), defBlocks[i].end());  // 工作表

            size_t ptr = 0;
            while (ptr < W.size())
            {
                int X = W[ptr++];
                if (X >= (int)DF.size()) continue;

                for (int Y : DF[X])
                {
                    if (F.find(Y) == F.end())
                    {
                        // 在块 Y 插入 Phi
                        Block*      blkY = function.blocks[Y];
                        DataType    dt   = allocas[i]->dt;
                        RegOperand* res  = OperandFactory::getInstance().getRegOperand(function.getNewRegId());
                        PhiInst*    phi  = new PhiInst(dt, res);

                        blkY->insts.push_front(phi);
                        blockPhis[Y][i] = phi;

                        F.insert(Y);
                        if (defBlocks[i].find(Y) == defBlocks[i].end()) { W.push_back(Y); }
                    }
                }
            }
        }

        // 5. 变量重命名
        std::vector<std::stack<Operand*>> stacks(allocas.size());
        OperandRename                     renamer;
        OperandMap                        renameMap;

        std::function<void(int)> rename = [&](int u) {
            Block*           blk = function.blocks[u];
            std::vector<int> pushCount(allocas.size(), 0);

            // 处理当前块的 Phi 定义
            if (blockPhis.count(u))
            {
                for (auto& [idx, phi] : blockPhis[u])
                {
                    stacks[idx].push(phi->res);
                    pushCount[idx]++;
                }
            }

            // 处理指令
            for (auto inst : blk->insts)
            {
                if (inst->opcode == Operator::LOAD)
                {
                    LoadInst* load = static_cast<LoadInst*>(inst);
                    if (load->ptr->getType() == OperandType::REG)
                    {
                        size_t reg = static_cast<RegOperand*>(load->ptr)->regNum;
                        if (regToAllocaIdx.count(reg))
                        {
                            int idx = regToAllocaIdx[reg];
                            if (!stacks[idx].empty()) { renameMap[load->res->getRegNum()] = stacks[idx].top(); }
                            else
                            {
                                // 使用未初始化的变量，用 0 代替
                                if (load->dt == DataType::F32)
                                    renameMap[load->res->getRegNum()] =
                                        OperandFactory::getInstance().getImmeF32Operand(0.0f);
                                else
                                    renameMap[load->res->getRegNum()] =
                                        OperandFactory::getInstance().getImmeI32Operand(0);
                            }
                            toRemove.insert(load);
                        }
                    }
                }
                else if (inst->opcode == Operator::STORE)
                {
                    StoreInst* store        = static_cast<StoreInst*>(inst);
                    bool       isPromotable = false;
                    if (store->ptr->getType() == OperandType::REG)
                    {
                        size_t reg = static_cast<RegOperand*>(store->ptr)->regNum;
                        if (regToAllocaIdx.count(reg))
                        {
                            isPromotable = true;
                            int      idx = regToAllocaIdx[reg];
                            Operand* val = store->val;
                            if (val->getType() == OperandType::REG)
                            {
                                size_t valReg = static_cast<RegOperand*>(val)->regNum;
                                if (renameMap.count(valReg)) { val = renameMap[valReg]; }
                            }
                            stacks[idx].push(val);
                            pushCount[idx]++;
                            toRemove.insert(store);
                        }
                    }

                    if (!isPromotable) { apply(renamer, *inst, renameMap); }
                }
                else if (inst->opcode == Operator::ALLOCA)
                {
                    AllocaInst* alloca = static_cast<AllocaInst*>(inst);
                    if (regToAllocaIdx.count(alloca->res->getRegNum())) { toRemove.insert(alloca); }
                }
                else
                {
                    // 其他指令，替换操作数
                    // 包括 PHI 指令
                    apply(renamer, *inst, renameMap);
                }
            }

            // 更新后继块的 Phi 参数
            Instruction*        term = blk->insts.back();
            std::vector<Block*> succs;
            if (term->opcode == Operator::BR_COND)
            {
                BrCondInst* br = static_cast<BrCondInst*>(term);
                succs.push_back(function.getBlock(static_cast<LabelOperand*>(br->trueTar)->lnum));
                succs.push_back(function.getBlock(static_cast<LabelOperand*>(br->falseTar)->lnum));
            }
            else if (term->opcode == Operator::BR_UNCOND)
            {
                BrUncondInst* br = static_cast<BrUncondInst*>(term);
                succs.push_back(function.getBlock(static_cast<LabelOperand*>(br->target)->lnum));
            }
            // RET 没有后继

            for (Block* succ : succs)
            {
                int v = (int)succ->blockId;
                if (blockPhis.count(v))
                {
                    for (auto& [idx, phi] : blockPhis[v])
                    {
                        Operand* val = nullptr;
                        if (!stacks[idx].empty()) { val = stacks[idx].top(); }
                        else
                        {
                            if (allocas[idx]->dt == DataType::F32)
                                val = OperandFactory::getInstance().getImmeF32Operand(0.0f);
                            else
                                val = OperandFactory::getInstance().getImmeI32Operand(0);
                        }
                        phi->addIncoming(val, OperandFactory::getInstance().getLabelOperand(u));
                    }
                }
            }

            // 递归访问支配树子节点
            const auto& domTree = domInfo->getDomTree();
            if (u < (int)domTree.size())
            {
                for (int v : domTree[u]) { rename(v); }
            }

            // 恢复栈状态
            for (size_t i = 0; i < allocas.size(); ++i)
            {
                for (int k = 0; k < pushCount[i]; ++k) { stacks[i].pop(); }
            }
        };

        rename(0);  // 从入口块开始

        // 6. 移除死指令
        for (auto& [id, block] : function.blocks)
        {
            std::deque<Instruction*> newInsts;
            for (auto inst : block->insts)
            {
                if (toRemove.find(inst) == toRemove.end()) { newInsts.push_back(inst); }
                else
                {
                    // 暂时不 delete，避免 double free 或其他问题，DCE 会处理
                }
            }
            block->insts = newInsts;
        }

        return true;
    }
}  // namespace ME
