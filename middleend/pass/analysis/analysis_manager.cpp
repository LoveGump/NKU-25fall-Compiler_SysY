#include <middleend/pass/analysis/analysis_manager.h>

namespace ME::Analysis
{
    Manager& AM = Manager::getInstance();

    Manager::~Manager()
    {
        for (auto& funcCachePair : analysisCache)
        {
            for (auto& analysisPair : funcCachePair.second)
            {
                auto deleterIt = deleterMap.find(analysisPair.first);
                if (deleterIt != deleterMap.end()) deleterIt->second(analysisPair.second);
            }
        }
    }

    // 单例模式获取 Analysis Manager 实例
    Manager& Manager::getInstance()
    {
        static Manager instance;
        return instance;
    }

    void Manager::invalidate(Function& func)
    {
        // 删除该函数上的所有分析结果
        auto it = analysisCache.find(&func);
        if (it == analysisCache.end()) return;
        for (auto& analysisPair : it->second)
        {
            auto deleterIt = deleterMap.find(analysisPair.first);
            if (deleterIt != deleterMap.end()) deleterIt->second(analysisPair.second);
        }
        analysisCache.erase(it);
    }
}  // namespace ME::Analysis
