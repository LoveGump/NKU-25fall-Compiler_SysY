#ifndef __INTERFACES_MIDDLEEND_ANALYSIS_POSTDOMINFO_H__
#define __INTERFACES_MIDDLEEND_ANALYSIS_POSTDOMINFO_H__

#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <dom_analyzer.h>

namespace ME::Analysis
{
    // 后支配信息，在adce中使用
    class PostDomInfo
    {
      public:
        static inline const size_t TID = getTID<PostDomInfo>();  // 唯一类型 ID

        DomAnalyzer* postDomAnalyzer;  // 后支配分析器指针

      public:
        PostDomInfo();
        ~PostDomInfo();

        void build(CFG& cfg);  // 基于 CFG 构建后支配信息

        const std::vector<std::vector<int>>& getPostDomTree() const { return postDomAnalyzer->dom_tree; }
        const std::vector<std::set<int>>&    getPostDomFrontier() const { return postDomAnalyzer->dom_frontier; }
        const std::vector<int>&              getImmPostDom() const { return postDomAnalyzer->imm_dom; }
    };

    template <>
    PostDomInfo* Manager::get<PostDomInfo>(Function& func);
}  // namespace ME::Analysis

#endif  // __INTERFACES_MIDDLEEND_ANALYSIS_POSTDOMINFO_H__
