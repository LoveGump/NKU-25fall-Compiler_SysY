#include <middleend/pass/analysis/loop_info.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/dominfo.h>
#include <deque>
#include <algorithm>

namespace ME::Analysis
{
    LoopInfo::LoopInfo() = default;

    LoopInfo::~LoopInfo() = default;

    void LoopInfo::build(CFG& cfg, DomInfo& dom)
    {
        // 清理旧数据
        blockToLoop.clear();
        topLevelLoops.clear();
        allLoops.clear();

        const auto& imm_dom = dom.getImmDom();
        const auto& G_id    = cfg.G_id;
        const auto& invG_id = cfg.invG_id;

        // 用于合并同一 header 的多个回边
        std::map<size_t, std::unique_ptr<Loop>> loopMap;

        // 遍历所有边，检测回边
        for (size_t u = 0; u < G_id.size(); ++u)
        {
            for (size_t v : G_id[u])
            {
                // 如果 v 支配 u，则 u->v 是回边，v 是循环头
                if (!dominates(static_cast<int>(v), static_cast<int>(u), imm_dom)) continue;

                // 收集自然循环节点
                std::set<size_t> loopNodes;
                collectLoopNodes(v, u, invG_id, loopNodes);

                // 过滤掉不被 header 支配的节点（处理不可规约循环）
                for (auto it = loopNodes.begin(); it != loopNodes.end();)
                {
                    if (!dominates(static_cast<int>(v), static_cast<int>(*it), imm_dom))
                        it = loopNodes.erase(it);
                    else
                        ++it;
                }

                // 查找或创建该 header 的循环
                auto it = loopMap.find(v);
                if (it == loopMap.end())
                {
                    auto loop    = std::make_unique<Loop>();
                    loop->header = v;
                    loop->blocks = loopNodes;
                    loop->latches.insert(u);
                    loopMap[v] = std::move(loop);
                }
                else
                {
                    // 合并多个回边形成的循环节点
                    it->second->blocks.insert(loopNodes.begin(), loopNodes.end());
                    it->second->latches.insert(u);
                }
            }
        }

        // 将 loopMap 中的循环移动到 allLoops
        for (auto& [header, loop] : loopMap)
        {
            // 计算出口信息
            computeExitInfo(*loop, G_id);
            allLoops.push_back(std::move(loop));
        }

        // 构建循环嵌套关系
        buildLoopNesting();

        // 建立块到循环的映射
        buildBlockToLoopMap();
    }

    bool LoopInfo::dominates(int dom, int node, const std::vector<int>& imm_dom) const
    {
        if (dom == node) return true;
        if (dom < 0 || node < 0) return false;
        if (static_cast<size_t>(dom) >= imm_dom.size() || static_cast<size_t>(node) >= imm_dom.size()) return false;

        int cur = node;
        while (cur >= 0 && static_cast<size_t>(cur) < imm_dom.size())
        {
            int parent = imm_dom[cur];
            if (parent == dom) return true;
            if (parent == cur) break;  // 到达根节点
            cur = parent;
        }
        return false;
    }

    void LoopInfo::collectLoopNodes(
        size_t header, size_t latch, const std::vector<std::vector<size_t>>& invG, std::set<size_t>& loopNodes) const
    {
        // 从 latch 反向遍历，找到所有能到达 header 的节点
        std::deque<size_t> worklist;
        loopNodes.insert(header);
        loopNodes.insert(latch);
        worklist.push_back(latch);

        while (!worklist.empty())
        {
            size_t node = worklist.front();
            worklist.pop_front();

            if (node == header) continue;  // header 是边界，不继续向上
            if (node >= invG.size()) continue;

            for (size_t pred : invG[node])
            {
                if (!loopNodes.insert(pred).second) continue;  // 已访问
                if (pred == header) continue;
                worklist.push_back(pred);
            }
        }
    }

    void LoopInfo::computeExitInfo(Loop& loop, const std::vector<std::vector<size_t>>& G) const
    {
        loop.exitBlocks.clear();
        loop.exitingBlocks.clear();

        for (size_t blockId : loop.blocks)
        {
            if (blockId >= G.size()) continue;

            for (size_t succ : G[blockId])
            {
                if (!loop.contains(succ))
                {
                    // succ 不在循环内，是出口块
                    loop.exitBlocks.insert(succ);
                    // blockId 有边指向循环外，是退出块
                    loop.exitingBlocks.insert(blockId);
                }
            }
        }
    }

    void LoopInfo::buildLoopNesting()
    {
        // 按循环大小排序，小循环在前（内层循环块数少）
        std::vector<Loop*> sortedLoops;
        for (auto& loop : allLoops) sortedLoops.push_back(loop.get());

        std::sort(sortedLoops.begin(), sortedLoops.end(), [](const Loop* a, const Loop* b) {
            return a->blocks.size() < b->blocks.size();
        });

        // 为每个循环找到最小的包含它的循环作为父循环
        for (size_t i = 0; i < sortedLoops.size(); ++i)
        {
            Loop* inner = sortedLoops[i];
            for (size_t j = i + 1; j < sortedLoops.size(); ++j)
            {
                Loop* outer = sortedLoops[j];
                // 如果 outer 包含 inner 的 header，则 inner 是 outer 的子循环
                if (outer->contains(inner->header))
                {
                    inner->parent = outer;
                    outer->subLoops.push_back(inner);
                    break;  // 找到最小包含循环即可
                }
            }
        }

        // 收集顶层循环（没有父循环的循环）
        topLevelLoops.clear();
        for (auto& loop : allLoops)
        {
            if (loop->parent == nullptr) topLevelLoops.push_back(loop.get());
        }

        // 计算循环深度
        std::function<void(Loop*, int)> setDepth = [&](Loop* loop, int d) {
            loop->depth = d;
            for (Loop* sub : loop->subLoops) setDepth(sub, d + 1);
        };
        for (Loop* top : topLevelLoops) setDepth(top, 1);
    }

    void LoopInfo::buildBlockToLoopMap()
    {
        blockToLoop.clear();

        // 按循环大小从小到大处理，保证每个块映射到最内层循环
        std::vector<Loop*> sortedLoops;
        for (auto& loop : allLoops) sortedLoops.push_back(loop.get());

        std::sort(sortedLoops.begin(), sortedLoops.end(), [](const Loop* a, const Loop* b) {
            return a->blocks.size() < b->blocks.size();
        });

        for (Loop* loop : sortedLoops)
        {
            for (size_t blockId : loop->blocks)
            {
                // 只有当块还没有映射到更内层循环时才更新
                if (blockToLoop.find(blockId) == blockToLoop.end()) blockToLoop[blockId] = loop;
            }
        }
    }

    Loop* LoopInfo::getLoopFor(size_t blockId) const
    {
        auto it = blockToLoop.find(blockId);
        if (it != blockToLoop.end()) return it->second;
        return nullptr;
    }

    bool LoopInfo::isLoopHeader(size_t blockId) const
    {
        for (auto& loop : allLoops)
        {
            if (loop->header == blockId) return true;
        }
        return false;
    }

    bool LoopInfo::isSubLoopOf(const Loop* inner, const Loop* outer) const
    {
        if (!inner || !outer) return false;
        if (inner == outer) return true;

        const Loop* cur = inner->parent;
        while (cur)
        {
            if (cur == outer) return true;
            cur = cur->parent;
        }
        return false;
    }

    int LoopInfo::getLoopDepth(size_t blockId) const
    {
        Loop* loop = getLoopFor(blockId);
        if (!loop) return 0;
        return loop->depth;
    }

    template <>
    LoopInfo* Manager::get<LoopInfo>(Function& func)
    {
        // 检查缓存
        if (auto* cached = getCached<LoopInfo>(func)) return cached;

        // 获取依赖的分析
        auto* cfg = get<CFG>(func);
        auto* dom = get<DomInfo>(func);

        // 构建 LoopInfo
        auto* loopInfo = new LoopInfo();
        loopInfo->build(*cfg, *dom);
        cache<LoopInfo>(func, loopInfo);
        registerDeleter<LoopInfo>();
        return loopInfo;
    }
}  // namespace ME::Analysis
