#include <middleend/pass/analysis/postdominfo.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <dom_analyzer.h>

namespace ME::Analysis
{
    PostDomInfo::PostDomInfo() { postDomAnalyzer = new DomAnalyzer(); }

    PostDomInfo::~PostDomInfo() { delete postDomAnalyzer; }

    void PostDomInfo::build(CFG& cfg)
    {
        postDomAnalyzer->clear();

        // 寻找出口块，CFG 已经移除了不可达块
        std::vector<int> exitPoints;
        for (auto& [blockId, block] : cfg.id2block)
        {
            if (cfg.G_id[blockId].empty())
            {
                // 没有后继 = 出口块
                exitPoints.push_back((int)blockId);
            }
            else
            {
                // 检查是否包含 RET 指令
                for (auto* inst : block->insts)
                {
                    if (inst->opcode == ME::Operator::RET)
                    {
                        exitPoints.push_back((int)blockId);
                        break;
                    }
                }
            }
        }

        // 将 CFG 的反向图转换为 int 类型
        std::vector<std::vector<int>> invGraph_int;
        invGraph_int.resize(cfg.invG_id.size());
        for (size_t i = 0; i < cfg.invG_id.size(); ++i)
        {
            for (size_t pred : cfg.invG_id[i])
            {
                invGraph_int[i].push_back((int)pred);
            }
        }

        // 在反向图上计算支配树 = 后支配树
        postDomAnalyzer->solve(invGraph_int, exitPoints, false);
    }

    template <>
    PostDomInfo* Manager::get<PostDomInfo>(Function& func)
    {
        if (auto* cached = getCached<PostDomInfo>(func)) return cached;

        auto* cfg = get<CFG>(func);

        auto* postDomInfo = new PostDomInfo();
        postDomInfo->build(*cfg);
        cache<PostDomInfo>(func, postDomInfo);
        return postDomInfo;
    }
}  // namespace ME::Analysis
