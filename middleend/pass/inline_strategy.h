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
            int  instruction_count  = 0;      // 指令数
            bool has_loops          = false;  // 是否包含循环
            bool has_pointer_params = false;  // 是否有指针参数
            bool is_recursive       = false;  // 是否递归

            // 循环块集合：记录哪些块在循环内
            // 用回边识别循环体节点：如果 u 支配 v ，且存在边 u -> v，则边u -> v 为回边
            // u是回边源，所有能到达 u 且被u支配的节点都是循环体节点
            std::set<size_t> loop_blocks;
        };

        // 调用点信息，用于估计内联收益
        struct CallSiteInfo
        {
            Function* caller    = nullptr;  // 调用函数
            Function* callee    = nullptr;  // 被调用函数
            CallInst* call_inst = nullptr;  // 调用指令
            bool      in_loop   = false;    // 是否在循环内
        };

      public:
        InlineStrategy()  = default;
        ~InlineStrategy() = default;

        // 对模块进行统计分析，构建内联决策所需数据
        void analyze(Module& module);

        // 判断是否内联
        bool shouldInline(Function& caller, Function& callee, CallInst& call_inst) const;

        // 获取处理顺序 ， callee 优先于 caller
        std::vector<Function*> getProcessingOrder() const;

        // 提供给访问者的记录接口
        void recordCallSite(Function& func, Block& block, CallInst& call_inst);

        // 根据名称查找函数
        Function* findFunction(const std::string& name) const;

      private:
        Module* module_ = nullptr;

        // 基础数据结构：函数映射、统计信息、调用图与拓扑序
        std::map<std::string, Function*>         name_map;  // 函数名 -> 函数对象 映射
        std::map<Function*, FunctionInfo>        function_info; // 函数统计信息
        std::vector<CallSiteInfo>                call_sites;    // 调用点信息列表
        std::map<Function*, std::set<Function*>> call_graph;    // 调用图：caller -> {callee}
        std::vector<Function*>                   topo_order;    // 拓扑排序后的函数处理顺序

      private:
        // 分阶段构建统计信息
        void buildFunctionMap(Module& module);
        void collectFunctionInfo();
        void collectLoopInfo(Function& func, FunctionInfo& info);
        void collectCallSites(Function& func);
        void detectRecursion();
        void computeTopologicalOrder();

        // 简化版支配性判断，用于识别回边
        bool dominates(int dom, int node, const std::vector<int>& imm_dom) const;
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_INLINE_STRATEGY_H__
