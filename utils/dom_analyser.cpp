#include <dom_analyzer.h>
#include <debug.h>
#include <cassert>
#include <functional>
#include <algorithm>

/*
 * Lengauer–Tarjan (LT) 支配计算算法简述
 *
 * 基本概念（针对一张以入口 s 为根的控制流图）：
 * - 支配：若结点 d 在所有从 s 到 u 的路径上都出现，则称 d 支配 u。
 * - 直接支配 idom(u)：在所有“严格支配”（不含 u 本身）的结点中，离 u 最近的那个。
 * - DFS 树 T：从 s 做一次 DFS，得到父亲 parent 与 DFS 进入次序 dfn（本文用 0..n-1 的“次序号”表示）。
 * - 半支配 sdom(u)：满足“从某个结点 v 可以走到 u，且路径上除端点外的所有结点 dfn 都大于 dfn(u)”的所有 v 中，dfn
 * 最小者。
 * 直观理解：sdom(u) 是一类“绕过更小 dfn 的点”到达 u 的最早可能起点，它是计算 idom 的关键桥梁。
 *
 * LT 的两条核心公式（以 DFS 序回溯顺序计算）：
 * - sdom(u) = min{ v | v 是 u 的前驱且 dfn(v) < dfn(u) } 与 { sdom(eval(p)) | p 是 u 的前驱且 dfn(p) > dfn(u) }
 * 的最小者。 其中 eval/Link 由并查集维护，用于在“沿 DFS 树向上压缩”的同时，记录路径上的“最小祖先”。
 * - idom(u) = ( sdom(u) == sdom(eval(u)) ? sdom(u) : idom(eval(u)) )，并最终做一遍链压缩。
 *
 * 算法主流程：
 * 1) 从虚拟源（连接所有入口）出发做 DFS：分配 dfn，记录 parent，并建立 work 数组映射（block <-> dfn）。
 * 2) 自底向上（逆 DFS 序）遍历每个结点 u：
 *    - 用上一行公式合并“前驱中更小 dfn 候选”与“经由非树边的候选（通过 eval(p) 转为半支配者域）”，得到 sdom(u)。
 *    - 将 u Link 到其 parent，并把 u 插入 parent 的“半支配孩子集合”中，便于下一步求 idom。
 *    - 处理 parent 的半支配孩子集合：依据 sdom(mn[v]) 是否等于 parent 来判定 idom(v) 为 parent 或 mn[v]。
 * 3) 再做一次按 DFS 序的“idom 链压缩”，得到最终的直接支配者数组 imm_dom。
 * 4) 用 imm_dom 构建支配树 dom_tree；随后按每条边 u->v，沿着 idom 链把 v 加入从 u 到 idom(v) 之间结点的支配边界
 * dom_frontier。
 *
 * 备注：当 reverse=true 时，构造反图并以“所有出口”的虚拟源进行同样流程，即可计算“后支配”（post-dominator）。
 *
 * 本实现保留了上述流程的变量与骨架，并在若干关键点设置了 TODO，引导你补完 Eval/Link、DF 等细节。
 */

using namespace std;

DomAnalyzer::DomAnalyzer() {}

// graph: 输入图的邻接表表示
// entry_points: 入口点列表（正向支配时为入口，后向支配时为出口）
// reverse: 是否计算后支配（默认为 false，即计算正向支配）
void DomAnalyzer::solve(const vector<vector<int>>& graph, const vector<int>& entry_points, bool reverse)
{
    int node_count = graph.size();  // 原图节点数

    // 构建工作图：添加虚拟源节点，连接所有入口/出口
    int                 virtual_source = node_count;  // 增加的虚拟节点
    vector<vector<int>> working_graph;

    if (!reverse)
    {
        // 正向支配：直接使用原图
        working_graph = graph;
        working_graph.push_back(vector<int>());
        for (int entry : entry_points)
        {
            // 连接虚拟源到所有入口
            working_graph[virtual_source].push_back(entry);
        }
    }
    else
    {
        // 后支配：构建反图
        working_graph.resize(node_count + 1);
        for (int u = 0; u < node_count; ++u)
        {
            // 遍历所有节点
            for (int v : graph[u])
            {
                // 将边反向
                working_graph[v].push_back(u);
            }
        }
        // 连接虚拟源到所有出口
        working_graph.push_back(vector<int>());
        for (int exit : entry_points)
        {
            // 将虚拟源连接到所有出口节点
            working_graph[virtual_source].push_back(exit);
        }
    }
    // 调用核心构建函数
    build(working_graph, node_count + 1, virtual_source, entry_points);
}

void DomAnalyzer::build(
    const vector<vector<int>>& working_graph, int node_count, int virtual_source, const std::vector<int>& entry_points)
{
    (void)entry_points;                              // 避免未使用参数的编译警告
    vector<vector<int>> backward_edges(node_count);  // 前驱表，其实也就是反向图，可以查找某节点的所有前驱
    // TODO(Lab 4): 构建反向边表 backward_edges[v] = { 所有指向 v 的前驱 }
    // 提示：为了正确处理多入口点的情况，可以使用一个虚拟的“入口点”，让它指向所有实际入口点
    // 这主要是为了处理后支配树可能有多个入口的情况
    // 这里我们已经在 working_graph 中添加了虚拟源节点，并连接到了所有入口点/出口点
    for (int u = 0; u < node_count; ++u)
    {
        for (int v : working_graph[u]) { backward_edges[v].push_back(u); }
    }

    // 初始化结果容器
    dom_tree.clear();
    dom_tree.resize(node_count);
    dom_frontier.clear();
    dom_frontier.resize(node_count);
    imm_dom.clear();
    imm_dom.resize(node_count);

    // 支配数的序号从 1 开始，0 留给虚拟源节点
    int dfs_count = -1;
    // 统计 DFS 序号              根据 DFS 序号映射回原节点编号  DFS 父亲节点数组
    vector<int> block_to_dfs(node_count, 0), dfs_to_block(node_count), parent(node_count, 0);
    // 半支配者数组，记录每个节点v 的半支配者 semi_dom[v]
    vector<int> semi_dom(node_count);
    // 并查集结构：父亲节点与最小祖先节点
    vector<int>         dsu_parent(node_count), min_ancestor(node_count);
    vector<vector<int>> semi_children(node_count);

    // 初始化并查集与半支配者
    for (int i = 0; i < node_count; ++i)
    {
        dsu_parent[i]   = i;  // 初始的时候并查集的每个节点的父亲是自己
        min_ancestor[i] = i;  // 最小祖先是自己
        semi_dom[i]     = i;  // 半支配者也是自己
    }

    // 深度优先搜索 DFS 编号与父亲关系构建
    function<void(int)> dfs = [&](int block) {
        block_to_dfs[block]     = ++dfs_count;
        dfs_to_block[dfs_count] = block;
        semi_dom[block]         = block_to_dfs[block];  // 初始时半支配者为自己
        for (int next : working_graph[block])
        {
            // 遍历当前节点的所有后继节点
            if (!block_to_dfs[next])
            {
                // 如果后继节点尚未访问，则递归访问
                dfs(next);
                // 设置DFS树中的父节点
                parent[next] = block;
            }
        }
    };
    dfs(virtual_source);

    // TODO(Lab 4): 路径压缩并带最小祖先维护的 Find（Tarjan-Eval）
    // 依据半支配序比较，维护 min_ancestor，并做并查集压缩
    // u 是要查询的节点 ，返回值是 u 在并查集中的代表元节点
    auto dsu_find = [&](int u, const auto& self) -> int {
        if (dsu_parent[u] == u) return u;  // 如果 u 是根节点，直接返回
        // 递归查找父节点的根
        int root = self(dsu_parent[u], self);
        // 如果父节点的半支配者更小，更新最小祖先
        if (semi_dom[min_ancestor[dsu_parent[u]]] < semi_dom[min_ancestor[u]])
        {
            min_ancestor[u] = min_ancestor[dsu_parent[u]];
        }
        // 路径压缩
        dsu_parent[u] = root;
        return root;
    };

    // u 是 要查询的点
    // 返回值是 u 的最小祖先节点
    auto dsu_query = [&](int u) -> int {
        dsu_find(u, dsu_find);
        return min_ancestor[u];
    };

    // TODO(Lab 4): 逆 DFS 序回溯半支配与 idom 计算
    // 指引：
    // 1) 逆序遍历 dfs_id = dfs_count..1：令 curr = dfs_to_block[dfs_id]
    //    - 根据 LT 公式合并两类候选：
    //      a) 所有 pred->curr 且 dfn(pred) < dfn(curr) 的 pred
    //      b) 所有 pred->curr 且 dfn(pred) > dfn(curr) 的 sdom(eval(pred))
    //    取上述候选的 dfn 最小者作为 semi_dom[curr]
    // 2) Link(curr, parent[curr])：并查集父指向 parent[curr]，维护 min_ancestor
    //    将 curr 放入 semi_children[sdom[curr]]，以备下一步对 parent 的半支配孩子集合进行处理
    // 3) 对 parent[curr] 的半支配孩子集合中的每个 child：
    //      若 sdom(mn[child]) == parent[curr] 则 imm_dom[child] = parent[curr]
    //      否则 imm_dom[child] = mn[child]
    //    然后清空该集合（以免重复处理）
    // 注意：eval/Link 的细节由上方 dsu_find/self 与 dsu_parent/min_ancestor 完成
    for (int dfs_id = dfs_count; dfs_id > 0; --dfs_id)
    {                                     // 逆序遍历 DFS 序号
        int curr = dfs_to_block[dfs_id];  // 当前节点
        for (int pred : backward_edges[curr])
        {
            // 遍历 curr 的所有前驱节点
            if (block_to_dfs[pred] == 0 && pred != virtual_source) continue;  // 跳过未访问的节点

            // 根据 LT 公式合并两类候选：如果 dfn(pred) < dfn(curr) 则就是 pred，否则通过 dsu_query(pred) 找到最小祖先
            int eval_node = (block_to_dfs[pred] < block_to_dfs[curr]) ? pred : dsu_query(pred);
            if (semi_dom[eval_node] < semi_dom[curr])
            {
                // 如果半支配者更小，更新半支配者
                semi_dom[curr] = semi_dom[eval_node];
            }
        }

        int sdom_block = dfs_to_block[semi_dom[curr]];
        semi_children[sdom_block].push_back(curr);
        dsu_parent[curr] = parent[curr];

        int p = parent[curr];
        for (int child : semi_children[p])
        {
            int u = dsu_query(child);
            if (semi_dom[u] == semi_dom[child]) { imm_dom[child] = p; }
            else { imm_dom[child] = u; }
        }
        semi_children[p].clear();
    }

    // 直接支配者 idom 链压缩
    for (int dfs_id = 1; dfs_id <= dfs_count; ++dfs_id)
    {
        int curr = dfs_to_block[dfs_id];
        if (imm_dom[curr] != dfs_to_block[semi_dom[curr]]) imm_dom[curr] = imm_dom[imm_dom[curr]];
    }

    // 构建支配树（以 idom 为树边）
    for (int i = 0; i < node_count; ++i)
        if (block_to_dfs[i]) dom_tree[imm_dom[i]].push_back(i);

    dom_tree.resize(virtual_source);
    dom_frontier.resize(virtual_source);
    imm_dom.resize(virtual_source);

    // 移除虚拟源节点并调整入口节点的支配者
    // 对于每个实际入口节点，其直接支配者应该设为自己（或 -1 表示无支配者）
    for (int i = 0; i < virtual_source; ++i)
    {
        if (imm_dom[i] == virtual_source)
        {
            // 这是一个入口节点，它的直接支配者是虚拟源，我们将其设为自己
            imm_dom[i] = i;
        }
    }
    // 在支配树构建完成后，你还需要从里面移除本来并不存在的虚拟源节点
    // 同时，需要注意设置移除了虚拟源节点后的入口节点的支配者
    for (int i = 0; i < node_count; ++i)
    {
        if (imm_dom[i] == virtual_source) { imm_dom[i] = i; }
    }

    // TODO(Lab 4): 构建支配边界

    // 构建支配边界
    for (int block = 0; block < virtual_source; ++block)
    {
        if (!block_to_dfs[block]) continue;  // 跳过未访问的节点

        for (int succ : working_graph[block])
        {
            (void)succ;
            // 沿 idom 链向上，将 succ 放入 runner 的支配边界集合
            // 提示：注意处理根与虚拟源节点，避免死循环
            if (block_to_dfs[block] == 0 || block_to_dfs[succ] == 0) continue;
            int runner = block;
            while (runner != imm_dom[succ])
            {
                dom_frontier[runner].insert(succ);
                runner = imm_dom[runner];
                if (runner == imm_dom[runner] && runner != imm_dom[succ]) break;
            }
        }
    }
}

void DomAnalyzer::clear()
{
    dom_tree.clear();
    dom_frontier.clear();
    imm_dom.clear();
}
