#include <middleend/pass/inline_strategy.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/dominfo.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <algorithm>
#include <deque>

namespace ME
{
    void InlineStrategy::analyze(Module& module)
    {
        // 清空旧统计，重新分析当前模块
        module_ = &module;
        name_map.clear();
        function_info.clear();
        call_sites.clear();
        call_counts.clear();
        call_graph.clear();
        topo_order.clear();

        buildFunctionMap(module);
        collectFunctionInfo();

        // 收集循环信息与调用点信息
        for (auto* func : module.functions)
        {
            if (!func || !func->funcDef) continue;
            // 循环深度用于判断“循环内调用”
            collectLoopInfo(*func, function_info[func]);
            // 记录每个 call 的基本信息与上下文
            collectCallSites(*func);
        }

        // 调用统计/递归检测/处理顺序/复杂度评分
        updateCallCounts();
        detectRecursion();
        computeTopologicalOrder();
        computeComplexityScores();
    }

    void InlineStrategy::buildFunctionMap(Module& module)
    {
        // 函数名到函数对象的映射
        for (auto* func : module.functions)
        {
            if (func && func->funcDef) name_map[func->funcDef->funcName] = func;
        }
    }

    void InlineStrategy::collectFunctionInfo()
    {
        if (!module_) return;
        for (auto* func : module_->functions)
        {
            if (!func) continue;
            FunctionInfo info;
            info.block_count = static_cast<int>(func->blocks.size());

            if (func->funcDef)
            {
                // 记录指针参数，便于内联策略使用
                for (auto& arg : func->funcDef->argRegs)
                {
                    if (arg.first == DataType::PTR) info.has_pointer_params = true;
                }
            }

            // 统计指令数与基本块数
            int inst_count = 0;
            for (auto& [id, block] : func->blocks)
            {
                if (!block) continue;
                inst_count += static_cast<int>(block->insts.size());
            }
            info.instruction_count = inst_count;

            function_info[func] = info;
        }
    }

    void InlineStrategy::collectLoopInfo(Function& func, FunctionInfo& info)
    {
        auto* cfg = Analysis::AM.get<Analysis::CFG>(func);
        auto* dom = Analysis::AM.get<Analysis::DomInfo>(func);
        if (!cfg || !dom) return;

        const auto& imm_dom = dom->getImmDom();
        const auto& G_id    = cfg->G_id;
        const auto& invG_id = cfg->invG_id;

        std::map<size_t, int> loop_depth;
        bool                  has_loop = false;

        // 通过回边识别循环，并估计循环深度
        for (size_t u = 0; u < G_id.size(); ++u)
        {
            if (G_id[u].empty()) continue;
            for (size_t v : G_id[u])
            {
                // v 支配 u 时，u -> v 是一条回边
                if (!dominates(static_cast<int>(v), static_cast<int>(u), imm_dom)) continue;

                has_loop = true;
                std::set<size_t>   loop_nodes;
                std::deque<size_t> worklist;

                loop_nodes.insert(v);
                loop_nodes.insert(u);
                worklist.push_back(u);

                // 反向遍历 CFG，把能回到回边源的节点加入循环集合
                while (!worklist.empty())
                {
                    size_t node = worklist.front();
                    worklist.pop_front();

                    if (node >= invG_id.size()) continue;
                    for (size_t pred : invG_id[node])
                    {
                        if (loop_nodes.insert(pred).second) worklist.push_back(pred);
                    }
                }

                // 一个节点参与多个回边时，深度累加
                for (size_t node : loop_nodes) { loop_depth[node]++; }
            }
        }

        info.has_loops  = has_loop;
        info.loop_depth = std::move(loop_depth);
    }

    void InlineStrategy::collectCallSites(Function& func)
    {
        // 遍历指令，收集调用点信息
        for (auto& [id, block] : func.blocks)
        {
            if (!block) continue;
            for (auto* inst : block->insts)
            {
                if (!inst) continue;
                // 只关心 call 指令
                if (inst->opcode != Operator::CALL) continue;
                recordCallSite(func, *block, *static_cast<CallInst*>(inst));
            }
        }
    }

    void InlineStrategy::recordCallSite(Function& func, Block& block, CallInst& call_inst)
    {
        auto info_it = function_info.find(&func);
        if (info_it == function_info.end()) return;

        Function* callee = findFunction(call_inst.funcName);
        if (!callee) return;

        // 记录调用点的基本信息
        CallSiteInfo cs;
        cs.caller    = &func;
        cs.callee    = callee;
        cs.call_inst = &call_inst;
        cs.block_id  = block.blockId;

        // 使用循环深度估计执行频率
        auto depth_it = info_it->second.loop_depth.find(block.blockId);
        if (depth_it != info_it->second.loop_depth.end())
        {
            cs.nesting_level = depth_it->second;
            cs.in_loop       = depth_it->second > 0;
            // 简单地以 2^depth 估计执行频率上限
            cs.estimated_frequency = 1 << std::min(4, depth_it->second);
        }

        // 参数类型中包含指针时可提高内联收益
        for (auto& arg : call_inst.args)
        {
            if (arg.first == DataType::PTR) cs.has_pointer_args = true;
        }

        call_sites.push_back(cs);
        function_info[&func].call_count++;
        function_info[callee].called_count++;
        // 构建调用图边
        call_graph[&func].insert(callee);
    }

    void InlineStrategy::updateCallCounts()
    {
        // 统计 caller->callee 的调用次数
        call_counts.clear();
        for (auto& cs : call_sites)
        {
            if (!cs.caller || !cs.callee) continue;
            call_counts[{cs.caller, cs.callee}]++;
        }
    }

    void InlineStrategy::detectRecursion()
    {
        // 在调用图中标记递归函数
        std::set<Function*>    visited;
        std::vector<Function*> stack;

        std::function<void(Function*)> dfs = [&](Function* func) {
            if (!func) return;
            visited.insert(func);
            stack.push_back(func);

            for (auto* callee : call_graph[func])
            {
                if (!callee) continue;
                // 如果回到栈内节点，则该强连通分量为递归
                auto it = std::find(stack.begin(), stack.end(), callee);
                if (it != stack.end())
                {
                    for (; it != stack.end(); ++it) function_info[*it].is_recursive = true;
                    continue;
                }
                if (!visited.count(callee)) dfs(callee);
            }

            stack.pop_back();
        };

        for (auto& [func, info] : function_info)
        {
            if (!visited.count(func)) dfs(func);
        }
    }

    void InlineStrategy::computeTopologicalOrder()
    {
        // 基于调用图生成处理顺序，便于逐层内联
        topo_order.clear();
        std::set<Function*> visited;

        std::function<void(Function*)> dfs = [&](Function* func) {
            if (!func || visited.count(func)) return;
            visited.insert(func);
            // 先处理被调函数，再处理调用者
            for (auto* callee : call_graph[func]) dfs(callee);
            topo_order.push_back(func);
        };

        for (auto& [func, info] : function_info) dfs(func);
    }

    void InlineStrategy::computeComplexityScores()
    {
        // 计算简单的复杂度评分
        for (auto& [func, info] : function_info)
        {
            int complexity = 0;
            complexity += info.instruction_count;
            complexity += info.block_count * 2;
            complexity += info.call_count * 3;
            if (info.has_loops) complexity += 10;
            info.complexity_score = complexity;
        }
    }

    bool InlineStrategy::shouldInline(
        Function& caller, Function& callee, CallInst& call_inst, std::string* reason) const
    {
        // 采用参考项目的规则判断是否内联
        auto caller_it = function_info.find(&caller);
        auto callee_it = function_info.find(&callee);
        if (caller_it == function_info.end() || callee_it == function_info.end()) return false;

        const auto& caller_info = caller_it->second;
        const auto& callee_info = callee_it->second;

        if (&caller == &callee) return false;
        if (callee_info.is_recursive) return false;

        // 小函数/非常小函数直接内联
        bool flag1 = callee_info.instruction_count <= 30;
        bool flag5 = callee_info.instruction_count <= 15;
        // 合并规模阈值，限制内联后体量
        bool flag2 = (caller_info.instruction_count + callee_info.instruction_count) <= 200;
        // 指针参数函数倾向内联
        bool flag3 = callee_info.has_pointer_params && (&caller != &callee);

        bool                flag4 = false;
        const CallSiteInfo* cs    = findCallSite(caller, call_inst);
        // 循环中的调用更可能收益，允许稍大体量
        if (cs) flag4 = cs->in_loop && callee_info.instruction_count <= 50;

        bool result = flag1 || flag2 || flag3 || flag4 || flag5;

        if (reason)
        {
            if (flag5)
                *reason =
                    "Very small function (" + std::to_string(callee_info.instruction_count) + " instructions <= 15)";
            else if (flag1)
                *reason = "Small function (" + std::to_string(callee_info.instruction_count) + " instructions <= 30)";
            else if (flag2)
                *reason = "Combined size acceptable (" + std::to_string(caller_info.instruction_count) + " + " +
                          std::to_string(callee_info.instruction_count) + " = " +
                          std::to_string(caller_info.instruction_count + callee_info.instruction_count) + " <= 200)";
            else if (flag3)
                *reason = "Function has pointer parameters (good for optimization)";
            else if (flag4)
                *reason = "Call in loop with acceptable size (" + std::to_string(callee_info.instruction_count) +
                          " instructions <= 50)";
            else
                *reason = "Does not meet aggressive inlining criteria";
        }

        return result;
    }

    const InlineStrategy::FunctionInfo* InlineStrategy::getFunctionInfo(Function& func) const
    {
        auto it = function_info.find(&func);
        if (it == function_info.end()) return nullptr;
        return &it->second;
    }

    bool InlineStrategy::isRecursive(Function& func) const
    {
        auto it = function_info.find(&func);
        if (it == function_info.end()) return false;
        return it->second.is_recursive;
    }

    int InlineStrategy::getCallCount(Function& caller, Function& callee) const
    {
        auto it = call_counts.find({&caller, &callee});
        if (it == call_counts.end()) return 0;
        return it->second;
    }

    std::vector<Function*> InlineStrategy::getProcessingOrder() const { return topo_order; }

    Function* InlineStrategy::findFunction(const std::string& name) const
    {
        auto it = name_map.find(name);
        if (it == name_map.end()) return nullptr;
        return it->second;
    }

    const InlineStrategy::CallSiteInfo* InlineStrategy::findCallSite(Function& caller, CallInst& call_inst) const
    {
        for (const auto& cs : call_sites)
        {
            if (cs.caller == &caller && cs.call_inst == &call_inst) return &cs;
        }
        return nullptr;
    }

    bool InlineStrategy::wouldCauseTooMuchGrowth(Function& caller, Function& callee) const
    {
        // 估算内联造成的代码膨胀
        const int MAX_FUNCTION_SIZE = 500;
        const int MAX_TOTAL_GROWTH  = 200;

        auto caller_it = function_info.find(&caller);
        auto callee_it = function_info.find(&callee);
        if (caller_it == function_info.end() || callee_it == function_info.end()) return false;

        int caller_size = caller_it->second.instruction_count;
        int callee_size = callee_it->second.instruction_count;
        int call_count  = getCallCount(caller, callee);

        // 用“被调大小 * 调用次数”估计膨胀
        int estimated_growth = callee_size * call_count;
        return (caller_size + estimated_growth > MAX_FUNCTION_SIZE) || (estimated_growth > MAX_TOTAL_GROWTH);
    }

    bool InlineStrategy::dominates(int dom, int node, const std::vector<int>& imm_dom) const
    {
        // 通过 imm_dom 链向上判断支配关系
        if (dom == node) return true;
        if (node < 0 || static_cast<size_t>(node) >= imm_dom.size()) return false;

        int cur = node;
        // 沿着 idom 链向上直到根或命中 dom
        while (cur >= 0 && static_cast<size_t>(cur) < imm_dom.size())
        {
            cur = imm_dom[cur];
            if (cur == dom) return true;
            if (cur < 0) break;
            if (cur == imm_dom[cur]) break;
        }
        return false;
    }
}  // namespace ME
