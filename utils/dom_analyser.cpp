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
 * 最小者。 直观理解：sdom(u) 是一类“绕过更小 dfn 的点”到达 u 的最早可能起点，它是计算 idom 的关键桥梁。
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

void DomAnalyzer::solve(const vector<vector<int>>& graph, const vector<int>& entry_points, bool reverse)
{
    int node_count = graph.size();

    int                 virtual_source = node_count;
    vector<vector<int>> working_graph;

    if (!reverse)
    {
        working_graph = graph;
        working_graph.push_back(vector<int>());
        for (int entry : entry_points) working_graph[virtual_source].push_back(entry);
    }
    else
    {
        working_graph.resize(node_count + 1);
        for (int u = 0; u < node_count; ++u)
            for (int v : graph[u]) working_graph[v].push_back(u);

        working_graph.push_back(vector<int>());
        for (int exit : entry_points) working_graph[virtual_source].push_back(exit);
    }

    build(working_graph, node_count + 1, virtual_source, entry_points);
}

void DomAnalyzer::build(
    const vector<vector<int>>& working_graph, int node_count, int virtual_source, const std::vector<int>& entry_points)
{
    (void)entry_points;
    vector<vector<int>> backward_edges(node_count);
    // TODO(Lab 4): 构建反向边表 backward_edges[v] = { 所有指向 v 的前驱 }
    // 提示：为了正确处理多入口点的情况，可以使用一个虚拟的“入口点”，让它指向所有实际入口点
    // 这主要是为了处理后支配树可能有多个入口的情况
    // 构建反向边表 backward_edges[v] = { 所有指向 v 的前驱 }
    for (int u = 0; u < node_count; ++u)
    {
        for (int v : working_graph[u]) { backward_edges[v].push_back(u); }
    }

    dom_tree.clear();
    dom_tree.resize(node_count);
    dom_frontier.clear();
    dom_frontier.resize(node_count);
    imm_dom.clear();
    imm_dom.resize(node_count);

    int                 dfs_count = -1;
    vector<int>         block_to_dfs(node_count, 0), dfs_to_block(node_count), parent(node_count, 0);
    vector<int>         semi_dom(node_count);
    vector<int>         dsu_parent(node_count), min_ancestor(node_count);
    vector<vector<int>> semi_children(node_count);

    for (int i = 0; i < node_count; ++i)
    {
        dsu_parent[i]   = i;
        min_ancestor[i] = i;
        semi_dom[i]     = i;
    }

    function<void(int)> dfs = [&](int block) {
        block_to_dfs[block]     = ++dfs_count;
        dfs_to_block[dfs_count] = block;
        semi_dom[block]         = block_to_dfs[block];
        for (int next : working_graph[block])
            if (!block_to_dfs[next])
            {
                dfs(next);
                parent[next] = block;
            }
    };
    dfs(virtual_source);

    // TODO(Lab 4): 路径压缩并带最小祖先维护的 Find（Tarjan-Eval）
    // 依据半支配序比较，维护 min_ancestor，并做并查集压缩
    auto dsu_find = [&](int u, const auto& self) -> int {
        // (void)self;
        // TODO("Lab4-Analysis: Implement Tarjan Eval/Link find with min-ancestor maintenance");
        // return u;
        if (dsu_parent[u] == u) return u;
        int root = self(dsu_parent[u], self);
        // 路径压缩：更新最小祖先
        if (semi_dom[min_ancestor[dsu_parent[u]]] < semi_dom[min_ancestor[u]])
            min_ancestor[u] = min_ancestor[dsu_parent[u]];
        dsu_parent[u] = root;
        return root;
    };

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
    // TODO("Lab4-Analysis: Reverse DFS pass to compute semi-dominators and initial idoms");
    // 逆 DFS 序回溯半支配与 idom 计算
    for (int dfs_id = dfs_count; dfs_id >= 1; --dfs_id)
    {
        int curr = dfs_to_block[dfs_id];

        // 计算半支配者 semi_dom[curr]
        for (int pred : backward_edges[curr])
        {
            if (!block_to_dfs[pred]) continue;  // 跳过未访问的节点
            int pred_dfn = block_to_dfs[pred];
            if (pred_dfn < block_to_dfs[curr])
            {
                // 情况 a: 前驱的 DFS 序小于当前节点
                if (pred_dfn < semi_dom[curr]) semi_dom[curr] = pred_dfn;
            }
            else
            {
                // 情况 b: 前驱的 DFS 序大于当前节点，需要通过 eval 找到最小半支配者
                int mn   = dsu_query(pred);
                int sdom = semi_dom[mn];
                if (sdom < semi_dom[curr]) semi_dom[curr] = sdom;
            }
        }

        // Link 操作：将当前节点链接到其父节点
        dsu_parent[curr] = parent[curr];

        // 将当前节点加入其半支配者的孩子集合
        semi_children[dfs_to_block[semi_dom[curr]]].push_back(curr);

        // 处理父节点的半支配孩子集合
        int par = parent[curr];
        for (int child : semi_children[par])
        {
            int mn = dsu_query(child);
            if (semi_dom[mn] == block_to_dfs[par])
                imm_dom[child] = par;
            else
                imm_dom[child] = mn;
        }
        semi_children[par].clear();
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
    // TODO("Lab4: Remove virtual source & adjust imm_dom");

    // TODO(Lab 4): 构建支配边界

    // 构建支配边界
    for (int block = 0; block < virtual_source; ++block)
    {
        if (!block_to_dfs[block]) continue;  // 跳过未访问的节点

        for (int succ : working_graph[block])
        {
            if (succ >= virtual_source) continue;  // 跳过虚拟源节点
            if (!block_to_dfs[succ]) continue;     // 跳过未访问的节点

            // 从 block 开始沿 idom 链向上，直到到达 succ 的直接支配者
            int runner = block;
            while (runner != imm_dom[succ])
            {
                // 沿 idom 链向上，将 succ 放入 runner 的支配边界集合
                // 提示：注意处理根与虚拟源节点，避免死循环
                // TODO("Lab4: Update dominance frontier along idom chain");

                // succ 在 runner 的支配边界中
                dom_frontier[runner].insert(succ);
                
                // 向上移动到父节点
                if (runner == imm_dom[runner]) break;  // 到达根节点
                runner = imm_dom[runner];
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
