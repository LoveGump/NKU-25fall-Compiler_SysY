#ifndef __MIDDLEEND_PASS_INLINE_STRATEGY_H__
#define __MIDDLEEND_PASS_INLINE_STRATEGY_H__

#include <middleend/module/ir_module.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ME
{
    // 内联策略：负责统计函数信息并判断是否应该内联
    class InlineStrategy
    {
      public:
        // 函数级统计信息，用于内联决策
        struct FunctionInfo
        {
            int  instruction_count  = 0;
            int  block_count        = 0;
            int  call_count         = 0;
            int  called_count       = 0;
            bool has_loops          = false;
            bool has_pointer_params = false;
            bool is_recursive       = false;
            int  complexity_score   = 0;

            std::map<size_t, int> loop_depth;  // blockId -> loop depth
        };

        // 调用点信息，用于估计内联收益
        struct CallSiteInfo
        {
            Function* caller             = nullptr;
            Function* callee             = nullptr;
            CallInst* call_inst          = nullptr;
            size_t    block_id           = 0;
            bool      in_loop            = false;
            int       estimated_frequency = 1;
            int       nesting_level      = 0;
            bool      has_pointer_args   = false;
        };

      public:
        InlineStrategy()  = default;
        ~InlineStrategy() = default;

        // 对模块进行统计分析，构建内联决策所需数据
        void analyze(Module& module);

        // 根据规则判断是否内联，并可返回原因说明
        bool shouldInline(
            Function& caller, Function& callee, CallInst& call_inst, std::string* reason = nullptr) const;

        // 查询函数信息或调用信息
        const FunctionInfo* getFunctionInfo(Function& func) const;
        bool                isRecursive(Function& func) const;
        int                 getCallCount(Function& caller, Function& callee) const;
        std::vector<Function*> getProcessingOrder() const;

        // 提供给访问者的记录接口
        void recordCallSite(Function& func, Block& block, CallInst& call_inst);

      private:
        Module* module_ = nullptr;

        // 基础数据结构：函数映射、统计信息、调用图与拓扑序
        std::map<std::string, Function*>             name_map;
        std::map<Function*, FunctionInfo>            function_info;
        std::vector<CallSiteInfo>                    call_sites;
        std::map<std::pair<Function*, Function*>, int> call_counts;
        std::map<Function*, std::set<Function*>>     call_graph;
        std::vector<Function*>                       topo_order;

      private:
        // 分阶段构建统计信息
        void buildFunctionMap(Module& module);
        void collectFunctionInfo();
        void collectLoopInfo(Function& func, FunctionInfo& info);
        void collectCallSites(Function& func);
        void updateCallCounts();
        void detectRecursion();
        void computeTopologicalOrder();
        void computeComplexityScores();

        // 内部辅助方法
        Function* findFunction(const std::string& name) const;
        const CallSiteInfo* findCallSite(Function& caller, CallInst& call_inst) const;
        bool                wouldCauseTooMuchGrowth(Function& caller, Function& callee) const;

        // 简化版支配性判断，用于识别回边
        bool dominates(int dom, int node, const std::vector<int>& imm_dom) const;
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_INLINE_STRATEGY_H__
