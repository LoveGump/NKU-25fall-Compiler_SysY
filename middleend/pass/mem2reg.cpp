#include <middleend/pass/mem2reg.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/rename_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/dominfo.h>
#include <algorithm>
#include <vector>
#include <stack>
#include <map>
#include <queue>
#include <unordered_set>
#include <unordered_map>

namespace ME
{
    void Mem2RegPass::runOnFunction(Function& function) { promoteMemoryToRegister(function); }

    bool Mem2RegPass::promoteMemoryToRegister(Function& function)
    {
        // 获取支配信息
        auto* domInfo = Analysis::AM.get<Analysis::DomInfo>(function);
        if (!domInfo) return false;

        // 1. 收集所有指令的使用情况
        UserCollector collector;
        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts) { apply(collector, *inst); }
        }

        // 2. 识别可提升的 Alloca 指令
        std::vector<AllocaInst*>                             allocas;         // 可提升的 Alloca 列表
        std::map<size_t, int>                                regToAllocaIdx;  // Alloca 寄存器编号 -> 索引
        std::unordered_set<Instruction*>                     toRemove;        // 待删除的指令集合
        std::vector<std::vector<std::pair<int, StoreInst*>>> storeInfo;  // allocaIdx -> (定义块ID, StoreInst) 列表

        for (auto& [blockId, block] : function.blocks)
        {
            for (auto inst : block->insts)
            {
                if (inst->opcode == Operator::ALLOCA)
                {
                    // 如果是alloca指令
                    AllocaInst* alloca = static_cast<AllocaInst*>(inst);
                    size_t      regNum = alloca->res->getRegNum();

                    auto it = collector.userMap.find(regNum);
                    if (it == collector.userMap.end())
                    {
                        // 无使用的 Alloca，标记删除
                        toRemove.insert(alloca);
                        continue;
                    }

                    // 检查是否可提升：只被 Load/Store 使用，且 Store 将其作为指针
                    bool  promotable = true;
                    auto& users      = it->second;
                    for (auto* user : users)
                    {
                        // 遍历所有使用该 Alloca 的指令
                        if (user->opcode == Operator::LOAD)
                        {
                            // Load 使用 alloca 作为指针，合法
                        }
                        else if (user->opcode == Operator::STORE)
                        {
                            StoreInst* store = static_cast<StoreInst*>(user);
                            // Store 必须将 alloca 作为指针，而不是作为值
                            if (store->ptr->getType() != OperandType::REG ||
                                store->ptr->getRegNum() != regNum)
                            {
                                promotable = false;
                                break;
                            }
                        }
                        else
                        {
                            // 其他指令使用（GEP, Call等）不支持提升
                            promotable = false;
                            break;
                        }
                    }

                    if (promotable)
                    {
                        int idx                = (int)allocas.size();  // 当前 Alloca 的索引
                        regToAllocaIdx[regNum] = idx;
                        allocas.push_back(alloca);  // 记录可提升的 Alloca
                        storeInfo.emplace_back();   // 初始化存储指令信息
                    }
                }
                else if (inst->opcode == Operator::STORE)
                {
                    // 如果是store指令，检查是否存储到可提升的Alloca
                    StoreInst* store = static_cast<StoreInst*>(inst);
                    if (store->ptr->getType() == OperandType::REG)
                    {
                        size_t reg = store->ptr->getRegNum();
                        auto   it  = regToAllocaIdx.find(reg);
                        if (it != regToAllocaIdx.end())
                        {
                            // 如果是存储到可提升的Alloca，记录 块ID和Store指令
                            int idx = it->second;
                            storeInfo[idx].emplace_back((int)blockId, store);
                        }
                    }
                }
            }
        }

        // 如果没有可提升的变量，直接返回
        if (allocas.empty() && toRemove.empty()) return false;

        // 单定义快速路径
        std::vector<bool> usesFastPath(allocas.size(), false);

        // 记录单一定义的operand
        std::vector<Operand*> singleDefValue(allocas.size(), nullptr);

        for (size_t i = 0; i < allocas.size(); ++i)
        {
            // 只有一个 Store 且在入口块
            // 可以直接替换所有 Load 为该 Store 的值
            if (storeInfo[i].size() == 1)
            {
                auto& [defBlockId, store] = storeInfo[i][0];
                if (defBlockId == 0)
                {
                    usesFastPath[i]   = true;
                    singleDefValue[i] = store->val;  // 记录唯一定义值
                    toRemove.insert(store);          // 移除该 Store 指令和 Alloca
                    toRemove.insert(allocas[i]);
                }
            }
        }

        // 插入 Phi 节点
        const auto&                                                DF = domInfo->getDomFrontier();  // 支配边界
        std::unordered_map<int, std::unordered_map<int, PhiInst*>> blockPhis;  // 块ID -> (allocaIdx -> PhiInst)

        for (size_t i = 0; i < allocas.size(); ++i)
        {
            // 遍历所有可提升的 Alloca
            // 如果这个 Alloca 使用了快速路径，则跳过
            if (usesFastPath[i]) continue;

            // 从 storeInfo 获取被 store 过的所有的基本块id
            std::unordered_set<int> defBlocks;
            for (auto& [blkId, _] : storeInfo[i]) { defBlocks.insert(blkId); }

            std::unordered_set<int> F;  // 已经给该 alloca(i) 插入了 Phi 节点的块id
            std::queue<int>         W;  // 工作队列，存储当前已知有定义的块id
            for (int blk : defBlocks) { W.push(blk); }

            while (!W.empty())
            {
                int X = W.front();
                W.pop();

                for (int Y : DF[X])
                {
                    // 遍历支配边界中的每个块Y
                    if (F.find(Y) == F.end())
                    {
                        // 如果还没有为Y插入phi节点
                        Block*      blkY = function.blocks[Y];  // 获取块Y
                        DataType    dt   = allocas[i]->dt;      // Alloca的数据类型
                        RegOperand* res =
                            OperandFactory::getInstance().getRegOperand(function.getNewRegId());  // 新寄存器
                        PhiInst* phi = new PhiInst(dt, res);                                      // 创建Phi节点

                        blkY->insts.push_front(phi);  // 将Phi节点插入块Y的指令列表前端
                        blockPhis[Y][(int)i] = phi;   // 记录该Phi节点

                        F.insert(Y);  // 将Y标记为已插入Phi节点
                        if (defBlocks.find(Y) == defBlocks.end())
                        {
                            // 如果Y不是定义块，继续传播，phi作为新def，所以需要继续处理
                            W.push(Y);
                        }
                    }
                }
            }
        }

        // 变量重命名

        // 每个alloca(i) 对应一个栈，储存这个节点最新的SSA值
        std::vector<std::stack<Operand*>> stacks(allocas.size());
        OperandRename                     renamer;    // 用于重命名寄存器的工具
        OperandMap                        renameMap;  // 记录重命名映射关系

        // 处理快速路径的 Alloca
        for (size_t i = 0; i < allocas.size(); ++i)
        {
            if (usesFastPath[i] && singleDefValue[i])
            {
                // 如果使用了快速路径，直接将 alloca(i) 替换为单定义值

                size_t allocaReg = allocas[i]->res->getRegNum();       // 获取alloca(i)的寄存器编号
                auto   it        = collector.userMap.find(allocaReg);  // 查找alloca(i)的所有用户
                if (it != collector.userMap.end())
                {
                    for (auto* user : it->second)
                    {
                        // 遍历 alloca(i) 的所有用户
                        if (user->opcode == Operator::LOAD)
                        {
                            // 如果是 Load 指令，将其结果重命名为单定义值
                            LoadInst* load = static_cast<LoadInst*>(user);

                            // 将 load 的结果重命名操作数
                            renameMap[load->res->getRegNum()] = singleDefValue[i];
                            toRemove.insert(load);  // 标记删除该 Load 指令
                        }
                    }
                }
            }
        }

        // 迭代式重命名
        const auto& domTree = domInfo->getDomTree();  // 获取支配树

        // 块ID , 阶段(0=进入块,1=退出块) , 每个alloca的压栈计数：
        std::stack<std::tuple<int, int, std::vector<int>>> workStack;

        // 工作栈，栈顶位置是当前路径上变量的最新值
        workStack.push({0, 0, std::vector<int>(allocas.size(), 0)});  // 从入口块开始

        while (!workStack.empty())
        {
            // 块id，阶段(0=进入块,1=退出块)，每个alloca的压栈计数
            auto [u, phase, pushCount] = workStack.top();
            workStack.pop();

            // 处理退出块
            if (phase == 1)
            {
                // 恢复栈状态
                for (size_t i = 0; i < allocas.size(); ++i)
                {
                    // 根据压栈次数对stack进行弹出
                    for (int k = 0; k < pushCount[i]; ++k) { stacks[i].pop(); }
                }
                continue;
            }

            // 获取当前块
            Block* blk = function.blocks[u];

            // 处理当前块的 Phi 定义
            auto phiIt = blockPhis.find(u);
            if (phiIt != blockPhis.end())
            {
                for (auto& [idx, phi] : phiIt->second)
                {
                    // 获取Phi指令的结果，将其压入对应栈
                    stacks[idx].push(phi->res);
                    pushCount[idx]++;  // 记录压栈次数
                }
            }

            // 处理指令，重命名
            for (auto inst : blk->insts)
            {
                // 如果是 Load/Store/Alloca，特殊处理
                if (inst->opcode == Operator::LOAD)
                {
                    // load是将结果进行更新，所以需要使用栈顶值
                    // 对于 Load 指令，使用栈顶值重命名结果
                    LoadInst* load = static_cast<LoadInst*>(inst);
                    if (load->ptr->getType() == OperandType::REG)
                    {
                        // 如果是从寄存器加载数据
                        size_t reg = load->ptr->getRegNum();
                        auto   it  = regToAllocaIdx.find(reg);
                        if (it != regToAllocaIdx.end() && !usesFastPath[it->second])
                        {
                            // 如果要存储的寄存器是alloca(i)的结果，并且没有使用快速路径
                            int idx = it->second;
                            if (!stacks[idx].empty())
                            {
                                // 对应栈不为空，使用栈顶值重命名结果
                                renameMap[load->res->getRegNum()] = stacks[idx].top();
                            }
                            else
                            {
                                // 如果栈为空，使用默认初始化值
                                if (load->dt == DataType::F32)
                                {
                                    renameMap[load->res->getRegNum()] =
                                        OperandFactory::getInstance().getImmeF32Operand(0.0f);
                                }
                                else
                                {
                                    renameMap[load->res->getRegNum()] =
                                        OperandFactory::getInstance().getImmeI32Operand(0);
                                }
                            }
                            toRemove.insert(load);  // 标记删除该 Load 指令
                        }
                    }
                }
                else if (inst->opcode == Operator::STORE)
                {
                    // 对于 Store 指令，更新对应栈
                    StoreInst* store        = static_cast<StoreInst*>(inst);
                    bool       isPromotable = false;
                    if (store->ptr->getType() == OperandType::REG)
                    {
                        // 如果是存储到寄存器
                        size_t reg = store->ptr->getRegNum();
                        auto   it  = regToAllocaIdx.find(reg);
                        if (it != regToAllocaIdx.end() && !usesFastPath[it->second])
                        {
                            // 如果要存储的寄存器是alloca(i)的结果，并且没有使用快速路径
                            isPromotable = true;  // 说明该 Store 可提升
                            int      idx = it->second;
                            Operand* val = store->val;
                            renameOperand(val, renameMap);  // 将要存的值进行重命名

                            stacks[idx].push(val);   // 将重命名后的值压入对应栈
                            pushCount[idx]++;        // 记录压栈次数
                            toRemove.insert(store);  // 标记删除该 Store 指令
                        }
                    }
                    // 如果是不可提升的 Store，则将ptr和val进行更新命名
                    if (!isPromotable) { apply(renamer, *inst, renameMap); }
                }
                else if (inst->opcode == Operator::ALLOCA)
                {
                    AllocaInst* alloca = static_cast<AllocaInst*>(inst);
                    if (regToAllocaIdx.count(alloca->res->getRegNum()))
                    {
                        // 可提升的 Alloca，标记删除
                        toRemove.insert(alloca);
                    }
                }
                else
                {
                    // 其他指令，也更新命名
                    apply(renamer, *inst, renameMap);
                }
            }

            // 更新后继块的 Phi 参数
            Instruction*        term = blk->insts.back();  // 获取终止指令
            std::vector<Block*> succs;                     // 存储后继块
            if (term->opcode == Operator::BR_COND)
            {
                // 如果终止指令是条件分支，将其真分支和假分支加入后继块列表
                BrCondInst* br = static_cast<BrCondInst*>(term);
                succs.push_back(function.getBlock(br->trueTar->getLabelNum()));
                succs.push_back(function.getBlock(br->falseTar->getLabelNum()));
            }
            else if (term->opcode == Operator::BR_UNCOND)
            {
                // 如果终止指令是无条件分支，将其目标块加入后继块列表
                BrUncondInst* br = static_cast<BrUncondInst*>(term);
                succs.push_back(function.getBlock(br->target->getLabelNum()));
            }

            for (Block* succ : succs)
            {
                // 遍历每个后继块
                int  v      = (int)succ->blockId;  // 后继块ID
                auto phiIt2 = blockPhis.find(v);   // 查找后继块的Phi节点
                if (phiIt2 != blockPhis.end())
                {
                    // 在后继块找到了Phi节点
                    for (auto& [idx, phi] : phiIt2->second)
                    {
                        // 对于每个Phi节点，添加来自当前块的参数
                        Operand* val = nullptr;
                        if (!stacks[idx].empty()) { val = stacks[idx].top(); }
                        else
                        {
                            // 如果栈为空，使用默认值
                            if (allocas[idx]->dt == DataType::F32)
                            {
                                val = OperandFactory::getInstance().getImmeF32Operand(0.0f);
                            }
                            else { val = OperandFactory::getInstance().getImmeI32Operand(0); }
                        }
                        phi->addIncoming(val, OperandFactory::getInstance().getLabelOperand(u));
                    }
                }
            }

            // 压入退出栈的任务，对stack的值进行更新
            workStack.push({u, 1, pushCount});

            // 处理深度优先遍历支配树的子节点，将子节点的load/store/alloca进行重命名
            // 逆序遍历以保持正确的DFS顺序，stack
            if (u < (int)domTree.size())
            {
                for (auto it = domTree[u].rbegin(); it != domTree[u].rend(); ++it)
                {
                    workStack.push({*it, 0, std::vector<int>(allocas.size(), 0)});
                }
            }
        }

        // 移除指令
        for (auto& [id, block] : function.blocks)
        {
            std::deque<Instruction*> newInsts;
            for (auto inst : block->insts)
            {
                if (toRemove.find(inst) == toRemove.end()) { newInsts.push_back(inst); }
                else
                {
                    // 删除可提升的 Alloca 指令
                    delete inst;
                }
            }
            block->insts = newInsts;
        }

        return true;
    }
}  // namespace ME
