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
        // 该过程会构建：
        // - 函数名映射（便于通过 call 的 funcName 找到 callee）
        // - 函数静态信息（指令数/块数/是否有指针参数等）
        // - 调用点列表与调用图（用于递归检测与处理顺序）
        // - 循环深度（用于估计调用执行频率，识别“循环内调用”）
        module_ = &module;
        name_map.clear();
        function_info.clear();
        call_sites.clear();
        call_graph.clear();
        topo_order.clear();

        // 构建函数映射与收集基本信息
        buildFunctionMap(module);

        // 收集函数级统计信息
        collectFunctionInfo();

        // 收集循环信息与调用点信息
        for (auto* func : module.functions)
        {
            if (!func || !func->funcDef) continue;
            // 循环深度用于判断 循环内调用
            collectLoopInfo(*func, function_info[func]);

            // 记录每个 call 的基本信息
            collectCallSites(*func);
        }

        // 调用统计/递归检测/处理顺序
        detectRecursion();
        computeTopologicalOrder();
    }

    void InlineStrategy::buildFunctionMap(Module& module)
    {
        // 函数名到函数对象的映射：只对有 funcDef 的定义函数建表
        for (auto* func : module.functions)
        {
            if (func && func->funcDef)
            {
                // 函数名 -> 函数对象 映射
                name_map[func->funcDef->funcName] = func;
            }
        }
    }

    void InlineStrategy::collectFunctionInfo()
    {
        if (!module_) return;
        for (auto* func : module_->functions)
        {
            // 遍历所有函数
            if (!func) continue;
            FunctionInfo info;

            // 统计是否有指针类型参数
            if (func->funcDef)
            {
                for (auto& arg : func->funcDef->argRegs)
                {
                    if (arg.first == DataType::PTR) { info.has_pointer_params = true; }
                }
            }

            // 统计指令数
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

        const auto& imm_dom = dom->getImmDom();  // 即时支配关系
        const auto& G_id    = cfg->G_id;
        const auto& invG_id = cfg->invG_id;

        std::map<size_t, int> loop_depth;  // blockId -> loop depth
        bool                  has_loop = false;

        // 通过回边识别循环，并估计循环深度：
        // - 若 v 支配 u，则 u -> v 是一条回边（back edge）
        // - 从回边源 u 沿反向 CFG 追溯可达节点，得到循环体节点集合
        // - 同一个节点若属于多个回边形成的循环体，深度累加
        for (size_t u = 0; u < G_id.size(); ++u)
        {
            // 遍历所有边
            if (G_id[u].empty()) continue;
            for (size_t v : G_id[u])
            {
                // v 支配 u 时，u -> v 是一条回边
                if (!dominates(static_cast<int>(v), static_cast<int>(u), imm_dom)) continue;

                has_loop = true;
                std::set<size_t>   loop_nodes;  // 循环体节点集合
                std::deque<size_t> worklist;    // 反向遍历工作队列

                loop_nodes.insert(v);
                loop_nodes.insert(u);
                worklist.push_back(u);

                // 反向遍历 CFG，把能回到回边源（u）的节点加入循环集合
                while (!worklist.empty())
                {
                    size_t node = worklist.front();
                    worklist.pop_front();

                    if (node >= invG_id.size()) continue;
                    for (size_t pred : invG_id[node])
                    {
                        // 遍历前驱节点，统计过的跳过
                        if (loop_nodes.insert(pred).second)
                        {
                            // 将所有能到达回边源的节点加入循环体
                            worklist.push_back(pred);
                        }
                    }
                }

                // 一个节点参与多个回边时，深度累加
                for (size_t node : loop_nodes) { loop_depth[node]++; }
            }
        }

        info.has_loops = has_loop;
        // 初步估计的循环深度
        info.loop_depth = loop_depth;
    }

    void InlineStrategy::collectCallSites(Function& func)
    {
        for (auto& [id, block] : func.blocks)
        {
            // 遍历所有基本块
            if (!block) continue;
            for (auto* inst : block->insts)
            {
                // 遍历所有的调用指令，统计所有的调用点信息
                if (!inst) continue;
                // 只关心 call 指令
                if (inst->opcode != Operator::CALL) continue;
                recordCallSite(func, *block, *static_cast<CallInst*>(inst));
            }
        }
    }

    // 统计函数调用点信息
    void InlineStrategy::recordCallSite(Function& func, Block& block, CallInst& call_inst)
    {
        auto info_it = function_info.find(&func);  // 调用者信息
        if (info_it == function_info.end()) return;

        // 通过函数名找到被调用函数
        Function* callee = findFunction(call_inst.funcName);
        if (!callee) return;

        // 统计调用点信息
        CallSiteInfo cs;
        cs.caller    = &func;
        cs.callee    = callee;
        cs.call_inst = &call_inst;

        // 判断是否在循环内
        auto depth_it = info_it->second.loop_depth.find(block.blockId);
        if (depth_it != info_it->second.loop_depth.end())
        {
            // 如果深度大于0，则在循环内
            cs.in_loop = depth_it->second > 0;
        }

        // 保存调用点信息
        call_sites.push_back(cs);
        // 构建调用图边
        call_graph[&func].insert(callee);
    }

    void InlineStrategy::detectRecursion()
    {
        // 在调用图中标记递归函数：
        // DFS 过程中维护当前递归栈，若访问到栈内节点则形成环
        // 将该环上的函数标记为递归，后续策略会直接拒绝内联递归函数
        std::set<Function*>    visited;  // 已访问节点
        std::vector<Function*> stack;    // 当前递归调用栈

        std::function<void(Function*)> dfs = [&](Function* func) {
            if (!func) return;
            visited.insert(func);

            stack.push_back(func);
            for (auto* callee : call_graph[func])
            {
                // 遍历被调用函数
                if (!callee) continue;
                // 如果回到栈内节点，则该强连通分量为递归
                auto it = std::find(stack.begin(), stack.end(), callee);
                if (it != stack.end())
                {
                    for (; it != stack.end(); ++it)
                    {
                        // 将栈上的函数标记为递归
                        function_info[*it].is_recursive = true;
                    }
                    continue;
                }
                if (!visited.count(callee))
                {
                    // 如果没有访问过，则继续 DFS
                    dfs(callee);
                }
            }
            // 弹出
            stack.pop_back();
        };

        for (auto& [func, info] : function_info)
        {
            // 遍历所有函数
            if (!visited.count(func)) { dfs(func); }
        }
    }

    void InlineStrategy::computeTopologicalOrder()
    {
        // 基于调用图生成处理顺序：
        // 采用 DFS 的后序，将 callee 先于 caller 放入 topo_order
        // 这样做更利于“先内联更底层/更小的函数”，减少重复工作
        topo_order.clear();
        std::set<Function*> visited;

        std::function<void(Function*)> dfs = [&](Function* func) {
            if (!func || visited.count(func)) return;
            visited.insert(func);
            // 先处理被调函数，再处理调用者
            for (auto* callee : call_graph[func])
            {
                // 递归 函数调用图 DFS
                dfs(callee);
            }
            topo_order.push_back(func);
        };

        for (auto& [func, info] : function_info)
        {
            // 遍历所有函数
            dfs(func);
        }
    }

    bool InlineStrategy::shouldInline(Function& caller, Function& callee, CallInst& call_inst) const
    {
        auto caller_it = function_info.find(&caller);
        auto callee_it = function_info.find(&callee);

        // 找不到信息则拒绝内联
        if (caller_it == function_info.end() || callee_it == function_info.end()) return false;

        const auto& caller_info = caller_it->second;
        const auto& callee_info = callee_it->second;

        // 拒绝递归内联
        if (&caller == &callee || callee_info.is_recursive) return false;

        // 策略要点：
        // - 拒绝递归内联（避免无限展开）
        // - 小函数倾向内联（降低 call 开销）
        // - 合并规模可控则允许（控制代码膨胀）
        // - 指针参数可能增加优化机会（如常量传播/别名信息更集中），倾向内联
        // - 循环内调用更偏向内联（按更高频率估计收益）

        // 被调用函数规模较小
        bool smallFunc = callee_info.instruction_count <= 30;
        // 合并规模可接受
        bool sizeOk = (caller_info.instruction_count + callee_info.instruction_count) <= 200;
        // 有指针参数
        bool hasPtr = callee_info.has_pointer_params;
        // 循环内调用更偏向内联
        bool inLoop = false;
        for (const auto& cs : call_sites)
        {
            if (cs.caller == &caller && cs.call_inst == &call_inst)
            {
                inLoop = cs.in_loop && callee_info.instruction_count <= 50;
                break;
            }
        }

        return smallFunc || sizeOk || hasPtr || inLoop;  //
    }

    std::vector<Function*> InlineStrategy::getProcessingOrder() const { return topo_order; }

    Function* InlineStrategy::findFunction(const std::string& name) const
    {
        auto it = name_map.find(name);
        if (it == name_map.end()) return nullptr;
        return it->second;
    }

    bool InlineStrategy::dominates(int dom, int node, const std::vector<int>& imm_dom) const
    {
        // 通过 imm_dom 链向上判断支配关系：从 node 沿 idom 向上追溯
        // 若能追溯到 dom，则 dom 支配 node
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
