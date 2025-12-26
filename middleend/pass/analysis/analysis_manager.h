#ifndef __INTERFACES_MIDDLEEND_ANALYSIS_MANAGER_H__
#define __INTERFACES_MIDDLEEND_ANALYSIS_MANAGER_H__

#include <functional>
#include <set>
#include <type_utils.h>
#include <unordered_map>
#include <vector>

/*
 * 中端分析管理器 (Analysis Manager)
 *
 * 用法速览:
 * - 注册/获取分析: 通过 AM.get<YourAnalysis>(function) 获得并缓存某函数上的分析结果。
 * - 缓存失效: 当函数 IR 发生改变后，调用 AM.invalidate(function) 使相关分析失效。
 * - 分析类需定义静态常量 TID = getTID<AP>()，用于唯一标识。
 *   该标识实际上是 getTID<AP>() 实例化后的函数地址。不同实例的 getTID<AP>()
 *   所在地址不同，因此我们可以将它用作每个类的唯一 ID
 * - 参考已有示例: CFG、DomInfo 的 get<> 特化与调用方式。
 */

namespace ME
{
    class Module;
    class Function;
    class Block;

    namespace Analysis
    {
        class Manager
        {
          private:
            // 此处使用 utils/type_utils.h 的 getTID 来为每个分析类生成一个唯一的 ID
            // 类型为 TID -> 分析结果指针(cfg, dominfo 等)
            using AnalysisMap = std::unordered_map<size_t, void*>;
            // 函数类指针 -> (分析 TID -> 分析结果 指针)
            std::unordered_map<Function*, AnalysisMap> analysisCache;

            using Deleter = void (*)(void*);
            // 分析 ID -> 删除器函数 指针
            std::unordered_map<size_t, Deleter> deleterMap;

            Manager() = default;
            ~Manager();

          public:
            static Manager& getInstance();

            // 获取某函数上的分析结果，若不存在则创建并缓存
            template <typename Target>
            Target* get(Function& func);

            // 使某函数上的所有分析结果失效
            void invalidate(Function& func);

          private:
            // 注册某分析类的删除器函数
            template <typename Target>
            void registerDeleter()
            {
                size_t tid = Target::TID;
                if (deleterMap.find(tid) == deleterMap.end())
                {
                    deleterMap[tid] = [](void* p) { delete static_cast<Target*>(p); };
                }
            }

            // 缓存某函数上的分析结果
            template <typename Target>
            void cache(Function& func, Target* analysis)
            {
                analysisCache[&func][Target::TID] = analysis;
            }

            // 获取某函数上已缓存的分析结果，若不存在则返回 nullptr
            template <typename Target>
            Target* getCached(Function& func)
            {
                // 检查缓存中是否已有该函数的指定分析结果
                if (analysisCache.count(&func))  // count 检查键是否存在
                {
                    auto& funcCache = analysisCache.at(&func);  // 获取该函数的分析结果映射
                    if (funcCache.count(Target::TID))
                    {
                        // 返回已缓存的分析结果
                        return static_cast<Target*>(funcCache.at(Target::TID));
                    }
                }
                return nullptr;
            }
        };

        extern Manager& AM;
    }  // namespace Analysis
}  // namespace ME

#endif  // __INTERFACES_MIDDLEEND_ANALYSIS_MANAGER_H__
