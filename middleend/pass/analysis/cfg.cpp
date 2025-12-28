#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <algorithm>

namespace ME::Analysis
{
    CFG::CFG() { id2block.clear(); }

    // 构建控制流图 CFG
    void CFG::build(ME::Function& function)
    {
        func = &function;
        id2block.clear();  // 清空当前cfg的block映射

        // 从函数中将blocks映射复制到id2block
        for (auto& [blockId, block] : function.blocks) id2block[blockId] = block;

        // 若函数无基本块则直接返回
        if (id2block.empty()) return;

        // 确定最大的 blockId
        size_t maxBlockId = 0;
        for (auto& [blockId, block] : id2block) maxBlockId = std::max(maxBlockId, blockId);

        G.clear();
        invG.clear();
        G_id.clear();
        invG_id.clear();

        G.resize(maxBlockId + 1);
        invG.resize(maxBlockId + 1);
        G_id.resize(maxBlockId + 1);
        invG_id.resize(maxBlockId + 1);

        std::map<size_t, bool> visited;  // 记录某 blockId 是否已访问
        buildFromBlock(0, visited);      // 构建cfg

        // 清理未访问的基本块及其边
        auto blocks_temp = func->blocks;
        func->blocks.clear();  // 清空函数的所有基本块映射

        for (auto& [blockId, block] : blocks_temp)
        {
            // 根据访问情况决定是否保留该基本块
            if (visited[blockId])
                func->blocks[blockId] = block;
            else
                delete block;
        }

        // 重新构建 id2block 映射
        id2block.clear();
        for (auto& [blockId, block] : func->blocks) id2block[blockId] = block;

        // 清理图中指向未访问基本块的边
        for (size_t i = 0; i <= maxBlockId; ++i)
        {
            if (!visited[i]) continue;

            auto& edges = G_id[i];
            // remove_if 将容器中满足条件的元素移到末尾，erase 删除这些元素
            edges.erase(
                std::remove_if(edges.begin(), edges.end(), [&](size_t target_id) { return !visited[target_id]; }),
                edges.end());
        }

        // 清理反向图中指向未访问基本块的边
        for (size_t i = 0; i <= maxBlockId; ++i)
        {
            if (!visited[i]) continue;

            auto& edges = invG_id[i];
            edges.erase(
                std::remove_if(edges.begin(), edges.end(), [&](size_t source_id) { return !visited[source_id]; }),
                edges.end());
        }
    }

    void CFG::buildFromBlock(size_t blockId, std::map<size_t, bool>& visited)
    {
        // 已经访问过或不存在该 blockId 则返回
        if (visited[blockId] || id2block.find(blockId) == id2block.end()) return;

        visited[blockId]        = true;
        ME::Block* currentBlock = id2block[blockId];  // 获取当前基本块指针

        // 查找终止指令
        Instruction* terminator = nullptr;
        for (auto it = currentBlock->insts.begin(); it != currentBlock->insts.end(); ++it)
        {
            if ((*it)->isTerminator())
            {
                terminator = *it;
                // 优化：删除基本块中终结指令之后的不可达指令
                auto next_it = std::next(it);
                while (next_it != currentBlock->insts.end())
                {
                    delete *next_it;
                    next_it = currentBlock->insts.erase(next_it);  // erase 返回下一个迭代器
                }
                break;
            }
        }

        if (!terminator) return;  // 无终止指令则返回

        if (terminator->opcode == Operator::BR_COND)
        {
            // 条件分支指令
            BrCondInst* brInst = static_cast<BrCondInst*>(terminator);

            if (brInst->trueTar->getType() == OperandType::LABEL && brInst->falseTar->getType() == OperandType::LABEL)
            {
                // 获取目标标签
                size_t trueLabelId  = brInst->trueTar->getLabelNum();
                size_t falseLabelId = brInst->falseTar->getLabelNum();

                // 连接true分支
                if (id2block.find(trueLabelId) != id2block.end())
                {
                    G[blockId].push_back(id2block[trueLabelId]);
                    G_id[blockId].push_back(trueLabelId);
                    invG[trueLabelId].push_back(currentBlock);
                    invG_id[trueLabelId].push_back(blockId);

                    buildFromBlock(trueLabelId, visited);
                }

                // 连接false分支
                if (id2block.find(falseLabelId) != id2block.end())
                {
                    G[blockId].push_back(id2block[falseLabelId]);
                    G_id[blockId].push_back(falseLabelId);
                    invG[falseLabelId].push_back(currentBlock);
                    invG_id[falseLabelId].push_back(blockId);

                    buildFromBlock(falseLabelId, visited);
                }
            }
        }
        else if (terminator->opcode == Operator::BR_UNCOND)
        {
            // 无条件分支指令
            BrUncondInst* brInst = static_cast<BrUncondInst*>(terminator);

            if (brInst->target->getType() == OperandType::LABEL)
            {
                size_t targetLabelId = brInst->target->getLabelNum();

                // 连接目标分支
                if (id2block.find(targetLabelId) != id2block.end())
                {
                    G[blockId].push_back(id2block[targetLabelId]);
                    G_id[blockId].push_back(targetLabelId);
                    invG[targetLabelId].push_back(currentBlock);
                    invG_id[targetLabelId].push_back(blockId);

                    buildFromBlock(targetLabelId, visited);
                }
            }
        }
    }

    template <>
    CFG* Manager::get<CFG>(Function& func)
    {
        // 检查缓存中是否已有该函数的 CFG 分析结果
        if (auto* cached = getCached<CFG>(func)) return cached;

        // 构建新的 CFG 分析结果并缓存
        auto* cfg = new CFG();
        cfg->build(func);
        cache<CFG>(func, cfg);
        return cfg;
    }
}  // namespace ME::Analysis
