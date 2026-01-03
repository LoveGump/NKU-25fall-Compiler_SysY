#include <backend/common/cfg_builder.h>

namespace BE::MIR
{
    CFG* CFGBuilder::buildCFGForFunction(BE::Function* func)
    {
        if (func == nullptr || func->blocks.empty()) return nullptr;
        CFG* cfg = new CFG();
        for (auto& [id, block] : func->blocks) cfg->addNewBlock(id, block);
        if (func->blocks.find(0) != func->blocks.end()) cfg->entry_block = func->blocks[0];
        for (auto& [id, block] : func->blocks)
        {
            auto targets = getBlockTargets(func, block);
            for (auto t : targets)
                if (cfg->blocks.count(t)) cfg->makeEdge(block->blockId, t);
        }
        addFallthroughEdges(func, cfg);
        for (auto& [id, block] : func->blocks)
        {
            if (block->insts.empty()) continue;
            auto* last = block->insts.back();
            if (adapter_->isReturn(last))
            {
                cfg->ret_block = block;
                break;
            }
        }
        return cfg;
    }

    static int nextBlockId(BE::Function* func, uint32_t curId)
    {
        auto it = func->blocks.upper_bound(curId);
        if (it == func->blocks.end()) return -1;
        return static_cast<int>(it->first);
    }

    std::vector<uint32_t> CFGBuilder::getBlockTargets(BE::Function* func, BE::Block* block)
    {
        std::vector<uint32_t> targets;
        if (block->insts.empty()) return targets;
        for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
        {
            auto* inst = *it;
            if (adapter_->isReturn(inst)) return targets;

            if (adapter_->isCondBranch(inst))
            {
                int t = adapter_->extractBranchTarget(inst);
                if (t >= 0) targets.push_back(static_cast<uint32_t>(t));
                // 尝试读取紧随其后的无条件跳转作为 false 分支
                auto nextIt = std::next(it);
                if (nextIt != block->insts.end())
                {
                    auto* nextInst = *nextIt;
                    if (adapter_->isUncondBranch(nextInst))
                    {
                        int ft = adapter_->extractBranchTarget(nextInst);
                        if (ft >= 0) targets.push_back(static_cast<uint32_t>(ft));
                    }
                    else
                    {
                        // 没有显式 false 分支，默认落入后继基本块
                        int fall = nextBlockId(func, block->blockId);
                        if (fall >= 0) targets.push_back(static_cast<uint32_t>(fall));
                    }
                }
                else
                {
                    // 没有显式 false 分支，默认落入后继基本块
                    int fall = nextBlockId(func, block->blockId);
                    if (fall >= 0) targets.push_back(static_cast<uint32_t>(fall));
                }
                // 已确定分支，返回
                return targets;
            }
            if (adapter_->isUncondBranch(inst))
            {
                int t = adapter_->extractBranchTarget(inst);
                if (t >= 0) targets.push_back(static_cast<uint32_t>(t));
                return targets;
            }
        }
        return targets;
    }

    void CFGBuilder::addFallthroughEdges(BE::Function* func, CFG* cfg)
    {
        for (auto& [id, block] : func->blocks)
        {
            if (block->insts.empty()) continue;

            MInstruction* lastBranch = nullptr;
            bool          isReturn   = false;
            for (auto it = block->insts.rbegin(); it != block->insts.rend(); ++it)
            {
                auto* inst = *it;
                if (adapter_->isReturn(inst))
                {
                    isReturn = true;
                    break;
                }
                if (adapter_->isUncondBranch(inst) || adapter_->isCondBranch(inst))
                {
                    lastBranch = inst;
                    break;
                }
            }

            if (isReturn) continue;

            bool need_fallthrough = false;
            if (lastBranch == nullptr)
            {
                need_fallthrough = true;
            }
            else if (adapter_->isCondBranch(lastBranch))
            {
                // 条件跳转且无显式 false 跳转，默认落入下一块
                need_fallthrough = true;
            }

            if (need_fallthrough)
            {
                int next_id = nextBlockId(func, id);
                if (next_id >= 0) cfg->makeEdge(id, static_cast<uint32_t>(next_id));
            }
        }
    }
}  // namespace BE::MIR
