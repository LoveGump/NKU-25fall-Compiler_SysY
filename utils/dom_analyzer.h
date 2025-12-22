#ifndef __UTILS_DOM_ANALYZER_H__
#define __UTILS_DOM_ANALYZER_H__

#include <vector>
#include <set>

// 支配分析器 (DomAnalyzer) 类
class DomAnalyzer
{
  public:
    std::vector<std::vector<int>> dom_tree;      // 支配树
    std::vector<std::set<int>>    dom_frontier;  // 支配边界
    std::vector<int>              imm_dom;       // 直接支配者

  public:
    DomAnalyzer();

  public:
    // 计算支配/后支配信息（基于 Lengauer-Tarjan 的总体流程）：
    // - 你需要补足若干中间步骤（并查集维护、半支配计算、idom 计算、支配边界收集）。
    // - 我们保留了输入/输出接口与核心调用骨架，细节请参考 TODO 注释与课堂资料。
    // graph: 输入图的邻接表表示
    // entry_points: 入口点列表（正向支配时为入口，后向支配时为出口）
    // reverse: 是否计算后支配（默认为 false，即计算正向支配）
    void solve(const std::vector<std::vector<int>>& graph, const std::vector<int>& entry_points, bool reverse = false);
    void clear();

  private:
    // TODO(Lab 4): 按 LT 算法完成该函数的关键步骤：
    // 1) DFS 编号与反图/父亲关系构建
    // 2) 半支配者 semi_dom 计算（含 Eval/Link 结构/最小祖先维护）
    // 3) 直接支配者 imm_dom 计算与压缩
    // 4) 支配树与支配边界构建
    void build(const std::vector<std::vector<int>>& working_graph, int node_count, int virtual_source,
        const std::vector<int>& entry_points);
};

#endif  // __UTILS_DOM_ANALYZER_H__
