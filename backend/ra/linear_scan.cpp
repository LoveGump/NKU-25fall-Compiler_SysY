#include <backend/ra/linear_scan.h>
#include <backend/mir/m_function.h>
#include <backend/mir/m_instruction.h>
#include <backend/mir/m_block.h>
#include <backend/mir/m_defs.h>
#include <backend/target/target_reg_info.h>
#include <backend/target/target_instr_adapter.h>
#include <backend/common/cfg.h>
#include <backend/common/cfg_builder.h>
#include <utils/dynamic_bitset.h>
#include <debug.h>

#include <map>
#include <set>
#include <unordered_map>
#include <deque>
#include <algorithm>

namespace BE::RA
{
    /*
     * 线性扫描寄存器分配（Linear Scan）教学版说明
     *
     * 目标：将每个虚拟寄存器（vreg）的活跃区间映射到目标机的物理寄存器或栈槽（溢出）。
     *
     * 核心步骤（整数/浮点分开执行，流程相同）：
     * 1) 指令线性化与编号：为函数内所有指令分配全局顺序号，记录每个基本块的 [start, end) 区间，
     *    同时收集调用点（callPoints），用于偏好分配被调用者保存寄存器（callee-saved）。
     * 2) 构建 USE/DEF：枚举每条指令的使用与定义寄存器，聚合到基本块级的 USE/DEF 集合。
     * 3) 活跃性分析：在 CFG 上迭代 IN/OUT，满足 IN = USE ∪ (OUT − DEF) 直至收敛。
     * 4) 活跃区间构建：按基本块从后向前，根据 IN/OUT 与指令次序，累积每个 vreg 的若干 [start, end) 段并合并。
     * 5) 标记跨调用：若区间与任意调用点重叠（交叉），标记 crossesCall=true，以便后续优先使用被调用者保存寄存器。
     * 6) 线性扫描分配：将区间按起点排序，维护活动集合 active；到达新区间时先移除已过期区间，然后
     *    尝试选择空闲物理寄存器；若无空闲则选择一个区间溢出（常见启发：溢出"结束点更远"的区间）。
     * 7) 重写 MIR：对未分配物理寄存器的 use/def，在指令前/后插入 reload/spill，并用临时物理寄存器替换操作数。
     *
     * 提示：
     * - 通过 TargetInstrAdapter 提供的接口完成目标无关的指令读写。
     * - TargetRegInfo 提供了可分配寄存器集合、被调用者保存寄存器、保留寄存器等信息。
     */
    namespace
    {
        //活跃区间片段
        struct Segment
        {
            int start;
            int end;
            Segment(int s = 0, int e = 0) : start(s), end(e) {}
        };

        struct Interval
        {
            BE::Register         vreg;
            std::vector<Segment> segs;
            bool                 crossesCall = false;
            int                  assignedReg = -1;    // 分配的物理寄存器，-1表示溢出
            int                  spillSlot   = -1;    //溢出槽索引

            //添加活跃区间片段
            void addSegment(int s, int e)
            {
                if (s >= e) return;
                segs.emplace_back(s, e);
            }

            //合并活跃区间片段
            void merge()
            {
                if (segs.empty()) return;
                // Sort segments by start point
                std::sort(segs.begin(), segs.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
                // Merge overlapping or adjacent segments
                std::vector<Segment> merged;
                merged.push_back(segs[0]);
                for (size_t i = 1; i < segs.size(); ++i)
                {
                    if (segs[i].start <= merged.back().end)
                    {
                        merged.back().end = std::max(merged.back().end, segs[i].end);
                    }
                    else
                    {
                        merged.push_back(segs[i]);
                    }
                }
                segs = std::move(merged);
            }

            // 获取起始点
            int getStart() const { return segs.empty() ? INT_MAX : segs.front().start; }
            // 获取结束点
            int getEnd() const { return segs.empty() ? 0 : segs.back().end; }

            // 检查某个点是否在区间内
            bool overlaps(int point) const
            {
                for (const auto& seg : segs)
                {
                    if (point >= seg.start && point < seg.end) return true;
                }
                return false;
            }

            // 检查两个区间是否重叠
            bool overlapsInterval(const Interval& other) const
            {
                for (const auto& s1 : segs)
                {
                    for (const auto& s2 : other.segs)
                    {
                        if (s1.start < s2.end && s2.start < s1.end) return true;
                    }
                }
                return false;
            }
        };

        // 区间比较器：按起始点排序
        struct IntervalOrder
        {
            bool operator()(const Interval* a, const Interval* b) const
            {
                // 按起始点排序，相同则按 vreg ID 排序（保证确定性）
                if (a->getStart() != b->getStart()) return a->getStart() < b->getStart();
                return a->vreg.rId < b->vreg.rId;
            }
        };

        // 活跃集合比较器：按结束点排序（方便移除过期区间）
        struct ActiveOrder
        {
            bool operator()(const Interval* a, const Interval* b) const
            {
                if (a->getEnd() != b->getEnd()) return a->getEnd() < b->getEnd();
                return a->vreg.rId < b->vreg.rId;
            }
        };

        // 判断是否为整数类型
        bool isIntegerType(BE::DataType* dt)
        {
            if (!dt) return true;
            return dt->dt == BE::DataType::Type::INT || dt->dt == BE::DataType::Type::TOKEN;
        }

        // 判断是否为浮点类型
        bool isFloatType(BE::DataType* dt)
        {
            return dt && dt->dt == BE::DataType::Type::FLOAT;
        }
    }  // namespace

    // 筛选可分配的整数寄存器列表
    static std::vector<int> buildAllocatableInt(const BE::Targeting::TargetRegInfo& ri)
    {
        std::vector<int> allocatable;//可分配的寄存器列表
        const auto&      reservedRegs = ri.reservedRegs();//保留的寄存器列表
        std::set<int>    reserved(reservedRegs.begin(), reservedRegs.end());//保留的寄存器集合

        // 只使用 callee-saved 寄存器进行分配。caller-saved 寄存器会被每次调用破坏，
        // 当前分配器不会在调用点周围插入保存/恢复指令。
        // 限制寄存器池可以避免跨调用的值被意外破坏，代价是可能会有更多溢出。
        const auto& calleeSaved = ri.calleeSavedIntRegs();// callee-saved 寄存器列表
        for (int r : calleeSaved)
        {
            if (!reserved.count(r)) allocatable.push_back(r);//加入可分配的寄存器列表
        }
        return allocatable;
    }

    // 构建可分配的浮点寄存器列表
    static std::vector<int> buildAllocatableFloat(const BE::Targeting::TargetRegInfo& ri)
    {
        std::vector<int> allocatable;
        const auto&      reservedRegs = ri.reservedRegs();
        std::set<int>    reserved(reservedRegs.begin(), reservedRegs.end());

        // 同样只使用 callee-saved 浮点寄存器
        const auto& calleeSaved = ri.calleeSavedFloatRegs();
        for (int r : calleeSaved)
        {
            if (!reserved.count(r)) allocatable.push_back(r);
        }
        return allocatable;
    }

    void LinearScanRA::allocateFunction(BE::Function& func, const BE::Targeting::TargetRegInfo& regInfo)
    {
        std::cerr << "[RA] function " << func.name << " begin" << std::endl;
        ASSERT(BE::Targeting::g_adapter && "TargetInstrAdapter is not set");

        std::cerr << "[RA] " << func.name << " step1 numbering" << std::endl;
        // ============================================================================
        // 第 1 步：指令编号
        // ============================================================================
        std::map<BE::Block*, std::pair<int, int>>                                   blockRange;//每个基本块的指令编号范围
        std::vector<std::pair<BE::Block*, std::deque<BE::MInstruction*>::iterator>> id2iter;//指令编号到指令的映射
        std::set<int>                                                               callPoints;//调用点
        int                                                                         ins_id = 0;//指令编号
        for (auto& [bid, block] : func.blocks)
        {
            //记录每个基本块的指令编号范围
            int start = ins_id;
            //记录指令编号到指令的映射
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it, ++ins_id)
            {
                id2iter.emplace_back(block, it);
                //记录调用点，调用点的作用是？
                // 记录调用点
                // 作用：后续用于判断哪些 vreg 的活跃区间跨越了函数调用
                // 跨调用的 vreg 必须分配到 callee-saved 寄存器，否则值会被调用破坏
                if (BE::Targeting::g_adapter->isCall(*it)) callPoints.insert(ins_id);
            }
            blockRange[block] = {start, ins_id};
        }

        std::cerr << "[RA] " << func.name << " step2 USE/DEF" << std::endl;
        // ============================================================================
        // 第 2 步：构建每个基本块的 USE/DEF 集合
        // ============================================================================
        std::map<BE::Block*, std::set<BE::Register>> USE, DEF;//USE/DEF 集合    
        //遍历每个基本块
        for (auto& [bid, block] : func.blocks)
        {
            // 初始化当前基本块的 USE 和 DEF 集合
            std::set<BE::Register> use, def;
            // 遍历基本块中的所有指令
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                std::vector<BE::Register> uses, defs;
                // 获取当前指令读取（uses）和写入（defs）的寄存器列表
                BE::Targeting::g_adapter->enumUses(*it, uses);
                BE::Targeting::g_adapter->enumDefs(*it, defs);
                // 记录基本块内定义的寄存器
                for (auto& d : defs)
                    if (!def.count(d)) def.insert(d);
                // 若寄存器在被当前块定义之前就被使用，则属于该块的 USE 集合（活跃性源头）
                for (auto& u : uses)
                    if (!def.count(u)) use.insert(u);
            }
            // 将计算出的集合存入映射表，用于后续的活跃性迭代分析
            USE[block] = std::move(use);
            DEF[block] = std::move(def);
        }

        std::cerr << "[RA] " << func.name << " step3 CFG" << std::endl;
        // ============================================================================
        // 第 3 步：构建 CFG 并获取后继关系
        // ============================================================================
        BE::MIR::CFGBuilder                           builder(BE::Targeting::g_adapter);
        BE::MIR::CFG*                                 cfg = builder.buildCFGForFunction(&func);
        std::map<BE::Block*, std::vector<BE::Block*>> succs;

        if (cfg)
        {
            for (auto& [id, block] : func.blocks)
            {
                succs[block] = {};
                if (id < cfg->graph.size())
                {
                    for (BE::Block* succ : cfg->graph[id])
                    {
                        if (succ) succs[block].push_back(succ);
                    }
                }
            }
        }

        std::cerr << "[RA] " << func.name << " step4 liveness" << std::endl;
        // ============================================================================
        // 第 4 步：活跃性分析（IN/OUT）
        // ============================================================================
        // IN[b] = USE[b] ∪ (OUT[b] − DEF[b])，OUT[b] = ⋃ IN[s]，s ∈ succs[b]
        std::map<BE::Block*, std::set<BE::Register>> IN, OUT;
        // 迭代计算 IN/OUT，直到收敛（不再变化）
        // 这是经典的后向数据流分析：信息从后继块向前驱块传播
        bool                                         changed = true;
        while (changed)
        {
            changed = false;
            for (auto& [bid, block] : func.blocks)
            {
                // 计算 OUT[block] = ⋃ IN[succ]，即所有后继块入口活跃寄存器的并集
                std::set<BE::Register> newOUT;
                for (auto* s : succs[block])
                {
                    auto it = IN.find(s);
                    if (it != IN.end()) newOUT.insert(it->second.begin(), it->second.end());
                }
                
                // 计算 IN[block] = USE[block] ∪ (OUT[block] - DEF[block])
                // 直觉：入口活跃 = 块内使用的 ∪ (出口活跃但不在块内定义的)
                std::set<BE::Register> newIN = USE[block];
                for (auto& r : newOUT)
                    if (!DEF[block].count(r)) newIN.insert(r);

                // 如果 IN 或 OUT 有变化，继续迭代
                if (newOUT != OUT[block] || newIN != IN[block])
                {
                    OUT[block] = std::move(newOUT);
                    IN[block]  = std::move(newIN);
                    changed    = true;
                }
            }
        }

        delete cfg;

        std::cerr << "[RA] " << func.name << " step5 intervals" << std::endl;
        // ============================================================================
        // 第 5 步：构建活跃区间
        // 活跃区间表示一个 vreg 在哪些指令编号范围内是活跃的
        // 例如：v1 在指令 [2, 7) 范围内活跃，意味着 v1 的值在这个范围内可能被使用
        // ============================================================================
        std::map<BE::Register, Interval> intervals;

        for (auto& [bid, block] : func.blocks)
        {
            auto [blockStart, blockEnd] = blockRange[block];

            // 如果寄存器在块出口活跃（OUT 中），说明它需要传递给后继块
            // 因此在整个块内都是活跃的
            for (const auto& r : OUT[block])
            {
                if (!r.isVreg) continue;
                intervals[r].vreg = r;
                intervals[r].addSegment(blockStart, blockEnd);
            }

            // 从后向前遍历指令构建区间
            // 为什么从后向前？因为我们需要先知道「使用点」才能确定活跃范围的终点
            int instIdx = blockEnd - 1;
            for (auto it = block->insts.rbegin(); it != block->insts.rend(); ++it, --instIdx)
            {
                std::vector<BE::Register> uses, defs;
                BE::Targeting::g_adapter->enumUses(*it, uses);
                BE::Targeting::g_adapter->enumDefs(*it, defs);

                // 定义点：vreg 在此处被定义，活跃区间从这里「开始」
                // 添加一个最小区间 [instIdx, instIdx+1) 表示定义点本身
                for (auto& d : defs)
                {
                    if (!d.isVreg) continue;
                    intervals[d].vreg = d;
                    intervals[d].addSegment(instIdx, instIdx + 1);
                }

                // 使用点：vreg 在此处被使用
                // 活跃范围从块开始延伸到使用点 [blockStart, instIdx+1)
                // 这样能确保值从定义点传递到使用点
                for (auto& u : uses)
                {
                    if (!u.isVreg) continue;
                    intervals[u].vreg = u;
                    intervals[u].addSegment(blockStart, instIdx + 1);
                }
            }
        }

        // 合并每个区间中的片段
        // 由于从多个块、多个使用点累积片段，可能有重叠或相邻的片段
        // 合并后得到简洁的活跃区间表示
        for (auto& [vreg, interval] : intervals)
        {
            interval.merge();
        }

        // 标记跨越调用点的区间
        // 如果 vreg 的活跃区间覆盖了某个 call 指令，必须使用 callee-saved 寄存器
        // 否则 call 会破坏 caller-saved 寄存器中的值
        for (auto& [vreg, interval] : intervals)
        {
            for (int callPt : callPoints)
            {
                if (interval.overlaps(callPt))
                {
                    interval.crossesCall = true;
                    break;
                }
            }
        }

        std::cerr << "[RA] " << func.name << " step6 allocate" << std::endl;
        // ============================================================================
        // 第 6 步：线性扫描主循环
        // ============================================================================
        auto allIntRegs   = buildAllocatableInt(regInfo);//可分配的整数寄存器列表
        auto allFloatRegs = buildAllocatableFloat(regInfo);//可分配的浮点寄存器列表

        // 按类型分离区间
        std::vector<Interval*> intIntervals, fpIntervals;//整数和浮点区间
        //遍历所有区间
        for (auto& [vreg, interval] : intervals)
        {
            if (isIntegerType(vreg.dt))//如果是整数类型
                intIntervals.push_back(&interval);//加入整数区间
            else if (isFloatType(vreg.dt))//如果是浮点类型
                fpIntervals.push_back(&interval);//加入浮点区间
        }

        // 按起始点排序
        std::sort(intIntervals.begin(), intIntervals.end(), IntervalOrder());
        std::sort(fpIntervals.begin(), fpIntervals.end(), IntervalOrder());

        // callee-saved 寄存器集合，用于优先级分配
        std::set<int> calleeSavedIntSet(regInfo.calleeSavedIntRegs().begin(), regInfo.calleeSavedIntRegs().end());
        std::set<int> calleeSavedFPSet(regInfo.calleeSavedFloatRegs().begin(), regInfo.calleeSavedFloatRegs().end());

        // 线性扫描分配的核心 lambda 函数
        // 参数：toAlloc - 待分配的区间列表（已按起始点排序）
        //       allocRegs - 可分配的物理寄存器列表
        //       calleeSaved - callee-saved 寄存器集合
        auto allocateIntervals = [&](std::vector<Interval*>& toAlloc, const std::vector<int>& allocRegs,
                                     const std::set<int>& calleeSaved) {
            // active: 当前活跃的区间集合，按结束点排序（方便移除过期区间）
            std::set<Interval*, ActiveOrder> active;
            // freeRegs: 当前空闲的物理寄存器
            std::set<int>                    freeRegs(allocRegs.begin(), allocRegs.end());

            // 溢出函数：将区间标记为溢出，并分配栈槽
            auto spillInterval = [&](Interval* iv) {
                if (!iv) return;
                iv->assignedReg = -1;  // 标记为未分配物理寄存器
                int spillWidth  = iv->vreg.dt ? iv->vreg.dt->getDataWidth() : 8;  // 溢出宽度（4/8字节）
                // 在栈帧中创建溢出槽
                if (iv->spillSlot < 0) iv->spillSlot = func.frameInfo.createSpillSlot(spillWidth);
            };

            // 按起始点顺序扫描每个区间
            for (Interval* interval : toAlloc)
            {
                int start = interval->getStart();

                // ========== Step 1: 移除已过期的区间，回收寄存器 ==========
                // 如果某个 active 区间的结束点 <= 当前起始点，说明它已经不再活跃
                for (auto it = active.begin(); it != active.end();)
                {
                    if ((*it)->getEnd() <= start)
                    {
                        // 回收寄存器到空闲池
                        if ((*it)->assignedReg >= 0) freeRegs.insert((*it)->assignedReg);
                        it = active.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                // ========== Step 2: 尝试分配空闲寄存器 ==========
                int chosenReg = -1;

                if (interval->crossesCall)
                {
                    // 跨调用的区间必须使用 callee-saved 寄存器
                    // 否则 call 会破坏 caller-saved 寄存器中的值
                    for (int r : freeRegs)
                    {
                        if (calleeSaved.count(r))
                        {
                            chosenReg = r;
                            break;
                        }
                    }
                }
                else if (!freeRegs.empty())
                {
                    // 不跨调用，任意空闲寄存器都可以
                    chosenReg = *freeRegs.begin();
                }

                // ========== Step 3: 分配成功或溢出 ==========
                if (chosenReg >= 0)
                {
                    // 分配成功！
                    interval->assignedReg = chosenReg;  // 记录分配的物理寄存器
                    freeRegs.erase(chosenReg);          // 从空闲池移除
                    active.insert(interval);            // 加入活跃集合
                }
                else
                {
                    // ========== Step 4: 无空闲寄存器，需要溢出 ==========
                    // 策略：选择结束点最远的区间溢出（它占用寄存器时间最长）
                    Interval* toSpill = nullptr;
                    for (auto* act : active)
                    {
                        // 如果当前区间跨调用，只考虑 callee-saved 寄存器
                        if (interval->crossesCall && !calleeSaved.count(act->assignedReg)) continue;
                        // 选择结束点最远的
                        if (!toSpill || act->getEnd() > toSpill->getEnd()) toSpill = act;
                    }

                    // 决定溢出谁
                    bool canTakeReg = toSpill && (!interval->crossesCall || calleeSaved.count(toSpill->assignedReg));
                    if (canTakeReg && toSpill->getEnd() > interval->getEnd())
                    {
                        // 情况 A：toSpill 活得更久，溢出它，把寄存器给当前区间
                        interval->assignedReg = toSpill->assignedReg;
                        spillInterval(toSpill);
                        active.erase(toSpill);
                        active.insert(interval);
                    }
                    else
                    {
                        // 情况 B：当前区间活得更久，溢出它自己
                        spillInterval(interval);
                    }
                }
            }
        };

        // 分别对整数和浮点区间进行分配
        allocateIntervals(intIntervals, allIntRegs, calleeSavedIntSet);    // 整数寄存器
        allocateIntervals(fpIntervals, allFloatRegs, calleeSavedFPSet);    // 浮点寄存器

        std::cerr << "[RA] " << func.name << " step7 rewrite" << std::endl;
        // ============================================================================
        // 第 7 步：重写 MIR
        // 将虚拟寄存器替换为物理寄存器，对溢出的 vreg 插入 load/store 指令
        // ============================================================================
        
        // 构建 vreg -> (physReg, spillSlot) 的映射表
        // physReg >= 0 表示分配了物理寄存器
        // spillSlot >= 0 表示溢出到栈
        std::map<BE::Register, std::pair<int, int>> vregToAssignment;
        for (auto& [vreg, interval] : intervals)
        {
            vregToAssignment[vreg] = {interval.assignedReg, interval.spillSlot};
        }

        // 获取临时寄存器池（用于溢出时的 load/store）
        // 临时寄存器从保留寄存器中选取，避免影响已分配的寄存器
        std::vector<int> scratchIntPool;    // 整数临时寄存器池
        std::vector<int> scratchFloatPool;  // 浮点临时寄存器池
        
        for (int r : regInfo.reservedRegs())
        {
            if (r >= 32)
                scratchFloatPool.push_back(r);  // 浮点寄存器 (f0-f31 编号为 32-63)
            else if (r != regInfo.spRegId() && r != regInfo.raRegId() && r != regInfo.zeroRegId() && r != 3 && r != 4 &&
                     r != 5)  // 排除 sp, ra, zero, gp, tp, t0（t0 被 lowering 用于大偏移）
                scratchIntPool.push_back(r);
        }
        // 如果没有可用的临时寄存器，使用最后一个可分配寄存器作为后备
        if (scratchIntPool.empty() && !allIntRegs.empty()) scratchIntPool.push_back(allIntRegs.back());
        if (scratchFloatPool.empty() && !allFloatRegs.empty()) scratchFloatPool.push_back(allFloatRegs.back());

        // 遍历每个基本块的每条指令
        for (auto& [bid, block] : func.blocks)
        {
            for (size_t idx = 0; idx < block->insts.size(); ++idx)
            {
                auto* inst = block->insts[idx];

                // 获取当前指令的使用和定义寄存器
                std::vector<BE::Register> uses, defs;
                BE::Targeting::g_adapter->enumUses(inst, uses);
                BE::Targeting::g_adapter->enumDefs(inst, defs);

                // 获取当前指令已占用的物理寄存器（避免冲突）
                std::vector<BE::Register>      physRegs;
                BE::Targeting::g_adapter->enumPhysRegs(inst, physRegs);
                std::set<int> busyPhys;
                for (auto& pr : physRegs) busyPhys.insert(pr.rId);

                // before: 要在当前指令前插入的指令（reload）
                // after:  要在当前指令后插入的指令（spill）
                std::vector<BE::MInstruction*> before, after;
                std::set<int>                   usedScratchInt, usedScratchFloat;
                std::vector<int>               useScratchIntList, useScratchFloatList;

                // ========== 处理使用点（uses）==========
                // 如果分配了物理寄存器：直接替换
                // 如果溢出了：先从栈加载到临时寄存器，再用临时寄存器替换
                for (const auto& u : uses)
                {
                    if (!u.isVreg) continue;
                    auto assignIt = vregToAssignment.find(u);
                    if (assignIt == vregToAssignment.end()) continue;

                    auto [physReg, spillSlot] = assignIt->second;
                    if (physReg >= 0)
                    {
                        // 情况 1：分配了物理寄存器，直接替换
                        BE::Register phys(physReg, u.dt, false);
                        BE::Targeting::g_adapter->replaceUse(inst, u, phys);
                    }
                    else if (spillSlot >= 0)
                    {
                        // 情况 2：溢出了，需要插入 reload 指令
                        bool isFloat = isFloatType(u.dt);
                        auto& pool   = isFloat ? scratchFloatPool : scratchIntPool;
                        auto& used   = isFloat ? usedScratchFloat : usedScratchInt;
                        
                        // 选择一个未被占用的临时寄存器
                        int   scratch = -1;
                        for (int r : pool)
                        {
                            if (used.count(r) || busyPhys.count(r)) continue;
                            scratch = r;
                            used.insert(r);
                            break;
                        }
                        if (scratch >= 0)
                        {
                            BE::Register scratchReg(scratch, u.dt, false);
                            // 在指令前插入：load scratch, spillSlot
                            before.push_back(new BE::FILoadInst(scratchReg, spillSlot, "reload from spill slot"));
                            // 用临时寄存器替换 vreg
                            BE::Targeting::g_adapter->replaceUse(inst, u, scratchReg);
                            if (isFloat)
                                useScratchFloatList.push_back(scratch);
                            else
                                useScratchIntList.push_back(scratch);
                        }
                    }
                }

                // ========== 处理定义点（defs）==========
                // 如果分配了物理寄存器：直接替换
                // 如果溢出了：用临时寄存器替换，然后存储到栈
                for (const auto& d : defs)
                {
                    if (!d.isVreg) continue;
                    auto assignIt = vregToAssignment.find(d);
                    if (assignIt == vregToAssignment.end()) continue;

                    auto [physReg, spillSlot] = assignIt->second;
                    if (physReg >= 0)
                    {
                        // 情况 1：分配了物理寄存器，直接替换
                        BE::Register phys(physReg, d.dt, false);
                        BE::Targeting::g_adapter->replaceDef(inst, d, phys);
                    }
                    else if (spillSlot >= 0)
                    {
                        // 情况 2：溢出了，需要插入 spill 指令
                        bool isFloat = isFloatType(d.dt);
                        auto& pool   = isFloat ? scratchFloatPool : scratchIntPool;
                        auto& used   = isFloat ? usedScratchFloat : usedScratchInt;
                        
                        // 选择一个临时寄存器
                        int   scratch = -1;
                        for (int r : pool)
                        {
                            if (used.count(r)) continue;
                            scratch = r;
                            used.insert(r);
                            break;
                        }
                        // 如果没有空闲的，复用之前 use 阶段用过的
                        if (scratch < 0)
                        {
                            if (!isFloat && !useScratchIntList.empty()) scratch = useScratchIntList.front();
                            if (isFloat && !useScratchFloatList.empty()) scratch = useScratchFloatList.front();
                        }
                        if (scratch >= 0)
                        {
                            BE::Register scratchReg(scratch, d.dt, false);
                            // 用临时寄存器替换 vreg
                            BE::Targeting::g_adapter->replaceDef(inst, d, scratchReg);
                            // 在指令后插入：store scratch, spillSlot
                            after.push_back(new BE::FIStoreInst(scratchReg, spillSlot, "spill to spill slot"));
                        }
                    }
                }

                // 插入 reload 指令（在当前指令前）
                if (!before.empty())
                {
                    block->insts.insert(block->insts.begin() + idx, before.begin(), before.end());
                    idx += before.size();  // 跳过插入的指令
                }

                // 插入 spill 指令（在当前指令后）
                if (!after.empty())
                {
                    block->insts.insert(block->insts.begin() + idx + 1, after.begin(), after.end());
                }
            }
        }

    }
}  // namespace BE::RA
