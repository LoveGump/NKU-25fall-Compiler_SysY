#ifndef __INTERFACES_MIDDLEEND_ANALYSIS_CFG_H__
#define __INTERFACES_MIDDLEEND_ANALYSIS_CFG_H__

#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/module/ir_function.h>
#include <map>
#include <vector>

/*
 * CFG (控制流图) 分析
 * - 通过 Analysis::AM.get<CFG>(function) 构建并缓存函数的基本块图。
 * - 提供 blockId->Block 的映射，以及正向/反向图与其 id 版本，便于后续分析使用。
 * - 必要时需调用 AM.invalidate(function) 来清理修改了结构的函数的 CFG 缓存。
 */

namespace ME
{
    class Function;
    class Block;
}  // namespace ME

namespace ME::Analysis
{
    class CFG
    {
      public:
        // 唯一类型 ID
        static inline const size_t TID = getTID<CFG>();

        ME::Function*                func;      // 所属函数指针
        std::map<size_t, ME::Block*> id2block;  // blockId -> Block*

        std::vector<std::vector<ME::Block*>> G{};     // 正向图
        std::vector<std::vector<ME::Block*>> invG{};  // 反向图

        std::vector<std::vector<size_t>> G_id{};     // 正向图的 id 版本
        std::vector<std::vector<size_t>> invG_id{};  // 反向图的 id 版本

      public:
        CFG();
        ~CFG() = default;
        // 构建函数内部的控制流图 CFG
        void build(ME::Function& function);
        void buildFromBlock(size_t blockId, std::map<size_t, bool>& visited);  // 从指定基本块递归构建 CFG
    };

    template <>
    CFG* Manager::get<CFG>(Function& func);  // 获取或构建某函数的 CFG 分析结果
}  // namespace ME::Analysis

#endif  // __INTERFACES_MIDDLEEND_ANALYSIS_CFG_H__
