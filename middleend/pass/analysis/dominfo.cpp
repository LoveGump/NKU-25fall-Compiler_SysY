#include <middleend/pass/analysis/dominfo.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <dom_analyzer.h>

namespace ME::Analysis
{
    DomInfo::DomInfo() { domAnalyzer = new DomAnalyzer(); }

    DomInfo::~DomInfo() { delete domAnalyzer; }

    void DomInfo::build(CFG& cfg)
    {
        domAnalyzer->clear();

        // 遍历所有基本块以确定出口点
        std::vector<int> exitPoints;
        for (auto& [blockId, block] : cfg.id2block)
        {
            for (auto* inst : block->insts)
            {
                if (!inst->isTerminator()) continue;
                if (inst->opcode != ME::Operator::RET) continue;

                exitPoints.push_back((int)blockId);
                break;
            }
        }

        // 将 CFG size_t -> int
        std::vector<std::vector<int>> graph_int;
        graph_int.resize(cfg.G_id.size());
        for (size_t i = 0; i < cfg.G_id.size(); ++i)
        {
            for (size_t successor : cfg.G_id[i]) { graph_int[i].push_back((int)successor); }
        }

        std::vector<int> entryPoints = {0};  // 入口点为基本块 0
        domAnalyzer->solve(graph_int, entryPoints, false);
    }

    template <>
    DomInfo* Manager::get<DomInfo>(Function& func)
    {
        if (auto* cached = getCached<DomInfo>(func)) return cached;

        auto* cfg = get<CFG>(func);

        auto* domInfo = new DomInfo();
        domInfo->build(*cfg);
        cache<DomInfo>(func, domInfo);
        return domInfo;
    }
}  // namespace ME::Analysis
