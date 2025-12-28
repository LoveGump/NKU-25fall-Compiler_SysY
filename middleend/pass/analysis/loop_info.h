#ifndef __INTERFACES_MIDDLEEND_ANALYSIS_LOOP_INFO_H__
#define __INTERFACES_MIDDLEEND_ANALYSIS_LOOP_INFO_H__

#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/dominfo.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_block.h>
#include <map>
#include <set>
#include <vector>
#include <memory>

/*
 * 循环信息 (LoopInfo) 分析
 * - 通过 Analysis::AM.get<LoopInfo>(function) 构建并缓存函数的循环信息。
 * - 提供循环识别、循环嵌套关系、循环成员查询等功能。
 * - 依赖 CFG 和 DomInfo 分析。
 */

namespace ME::Analysis
{
    // 循环结构
    struct Loop
    {
        size_t             header = 0;        // 循环头块 ID
        std::set<size_t>   blocks;            // 循环体所有块 ID
        std::set<size_t>   latches;           // 回边源块 ID（latch）
        std::set<size_t>   exitBlocks;        // 出口块 ID（循环外的后继）
        std::set<size_t>   exitingBlocks;     // 退出块 ID（有边指向循环外的循环内块）
        Loop*              parent = nullptr;  // 外层循环
        std::vector<Loop*> subLoops;          // 内层循环列表
        int                depth = 0;         // 循环嵌套深度（最外层为 1）

        // 判断某块是否在循环内
        bool contains(size_t blockId) const { return blocks.count(blockId) > 0; }

        // 判断某块是否是循环头
        bool isHeader(size_t blockId) const { return blockId == header; }

        // 判断某块是否是 latch
        bool isLatch(size_t blockId) const { return latches.count(blockId) > 0; }

        // 获取循环块数量
        size_t getNumBlocks() const { return blocks.size(); }
    };

    // 循环信息分析类
    class LoopInfo
    {
      public:
        static inline const size_t TID = getTID<LoopInfo>();  // 唯一类型 ID

      public:
        LoopInfo();
        ~LoopInfo();

        // 基于 CFG 和 DomInfo 构建循环信息
        void build(CFG& cfg, DomInfo& dom);

        // 获取某块所在的最内层循环，若不在任何循环中返回 nullptr
        Loop* getLoopFor(size_t blockId) const;

        // 获取所有顶层循环（不被其他循环包含的循环）
        const std::vector<Loop*>& getTopLevelLoops() const { return topLevelLoops; }

        // 获取所有循环
        const std::vector<std::unique_ptr<Loop>>& getAllLoops() const { return allLoops; }

        // 判断某块是否是某个循环的头
        bool isLoopHeader(size_t blockId) const;

        // 获取循环数量
        size_t getNumLoops() const { return allLoops.size(); }

        // 判断 inner 是否是 outer 的子循环（或相同循环）
        bool isSubLoopOf(const Loop* inner, const Loop* outer) const;

        // 获取循环深度（不在循环中返回 0）
        int getLoopDepth(size_t blockId) const;

      private:
        std::map<size_t, Loop*>            blockToLoop;    // 块 ID -> 最内层循环
        std::vector<Loop*>                 topLevelLoops;  // 顶层循环列表
        std::vector<std::unique_ptr<Loop>> allLoops;       // 所有循环（拥有所有权）

        // 辅助函数：判断 dom 是否支配 node
        bool dominates(int dom, int node, const std::vector<int>& imm_dom) const;

        // 从回边收集自然循环的所有节点
        void collectLoopNodes(size_t header, size_t latch, const std::vector<std::vector<size_t>>& invG,
            std::set<size_t>& loopNodes) const;

        // 计算循环的退出块和退出边源块
        void computeExitInfo(Loop& loop, const std::vector<std::vector<size_t>>& G) const;

        // 构建循环嵌套关系
        void buildLoopNesting();

        // 建立块到循环的映射
        void buildBlockToLoopMap();
    };

    template <>
    LoopInfo* Manager::get<LoopInfo>(Function& func);
}  // namespace ME::Analysis

#endif  // __INTERFACES_MIDDLEEND_ANALYSIS_LOOP_INFO_H__
