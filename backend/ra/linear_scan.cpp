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
            int                  assignedReg = -1;    // Assigned physical register ID, -1 if spilled
            int                  spillSlot   = -1;    // Spill slot index, -1 if not spilled

            void addSegment(int s, int e)
            {
                if (s >= e) return;
                segs.emplace_back(s, e);
            }

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

            int getStart() const { return segs.empty() ? INT_MAX : segs.front().start; }
            int getEnd() const { return segs.empty() ? 0 : segs.back().end; }

            bool overlaps(int point) const
            {
                for (const auto& seg : segs)
                {
                    if (point >= seg.start && point < seg.end) return true;
                }
                return false;
            }

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

        struct IntervalOrder
        {
            bool operator()(const Interval* a, const Interval* b) const
            {
                // Sort by start point, then by vreg ID for determinism
                if (a->getStart() != b->getStart()) return a->getStart() < b->getStart();
                return a->vreg.rId < b->vreg.rId;
            }
        };

        // Comparator for active set (sorted by end point for easy expiration)
        struct ActiveOrder
        {
            bool operator()(const Interval* a, const Interval* b) const
            {
                if (a->getEnd() != b->getEnd()) return a->getEnd() < b->getEnd();
                return a->vreg.rId < b->vreg.rId;
            }
        };

        bool isIntegerType(BE::DataType* dt)
        {
            if (!dt) return true;
            return dt->dt == BE::DataType::Type::INT || dt->dt == BE::DataType::Type::TOKEN;
        }

        bool isFloatType(BE::DataType* dt)
        {
            return dt && dt->dt == BE::DataType::Type::FLOAT;
        }
    }  // namespace

    static std::vector<int> buildAllocatableInt(const BE::Targeting::TargetRegInfo& ri)
    {
        std::vector<int> allocatable;
        const auto&      intRegs      = ri.intRegs();
        const auto&      reservedRegs = ri.reservedRegs();
        std::set<int>    reserved(reservedRegs.begin(), reservedRegs.end());

        // Include callee-saved registers first (they are preferred for cross-call intervals)
        const auto& calleeSaved = ri.calleeSavedIntRegs();
        for (int r : calleeSaved)
        {
            if (!reserved.count(r)) allocatable.push_back(r);
        }

        // Then include caller-saved registers (temporary registers)
        for (int r : intRegs)
        {
            if (!reserved.count(r) && std::find(allocatable.begin(), allocatable.end(), r) == allocatable.end())
            {
                allocatable.push_back(r);
            }
        }
        return allocatable;
    }

    static std::vector<int> buildAllocatableFloat(const BE::Targeting::TargetRegInfo& ri)
    {
        std::vector<int> allocatable;
        const auto&      floatRegs    = ri.floatRegs();
        const auto&      reservedRegs = ri.reservedRegs();
        std::set<int>    reserved(reservedRegs.begin(), reservedRegs.end());

        // Include callee-saved FP registers first
        const auto& calleeSaved = ri.calleeSavedFloatRegs();
        for (int r : calleeSaved)
        {
            if (!reserved.count(r)) allocatable.push_back(r);
        }

        // Then include caller-saved FP registers
        for (int r : floatRegs)
        {
            if (!reserved.count(r) && std::find(allocatable.begin(), allocatable.end(), r) == allocatable.end())
            {
                allocatable.push_back(r);
            }
        }
        return allocatable;
    }

    void LinearScanRA::allocateFunction(BE::Function& func, const BE::Targeting::TargetRegInfo& regInfo)
    {
        std::cerr << "[RA] function " << func.name << " begin" << std::endl;
        ASSERT(BE::Targeting::g_adapter && "TargetInstrAdapter is not set");

        std::cerr << "[RA] " << func.name << " step1 numbering" << std::endl;
        // ============================================================================
        // Step 1: Instruction numbering 
        // ============================================================================
        std::map<BE::Block*, std::pair<int, int>>                                   blockRange;
        std::vector<std::pair<BE::Block*, std::deque<BE::MInstruction*>::iterator>> id2iter;
        std::set<int>                                                               callPoints;
        int                                                                         ins_id = 0;
        for (auto& [bid, block] : func.blocks)
        {
            int start = ins_id;
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it, ++ins_id)
            {
                id2iter.emplace_back(block, it);
                if (BE::Targeting::g_adapter->isCall(*it)) callPoints.insert(ins_id);
            }
            blockRange[block] = {start, ins_id};
        }

        std::cerr << "[RA] " << func.name << " step2 USE/DEF" << std::endl;
        // ============================================================================
        // Step 2: Build USE/DEF sets for each block
        // ============================================================================
        std::map<BE::Block*, std::set<BE::Register>> USE, DEF;
        for (auto& [bid, block] : func.blocks)
        {
            std::set<BE::Register> use, def;
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                std::vector<BE::Register> uses, defs;
                BE::Targeting::g_adapter->enumUses(*it, uses);
                BE::Targeting::g_adapter->enumDefs(*it, defs);
                for (auto& d : defs)
                    if (!def.count(d)) def.insert(d);
                for (auto& u : uses)
                    if (!def.count(u)) use.insert(u);
            }
            USE[block] = std::move(use);
            DEF[block] = std::move(def);
        }

        std::cerr << "[RA] " << func.name << " step3 CFG" << std::endl;
        // ============================================================================
        // Step 3: Build CFG and get successor relations
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
        // Step 4: Liveness analysis (IN/OUT)
        // ============================================================================
        // IN[b] = USE[b] ∪ (OUT[b] − DEF[b])，OUT[b] = ⋃ IN[s]，s ∈ succs[b]
        std::map<BE::Block*, std::set<BE::Register>> IN, OUT;
        bool                                         changed = true;
        while (changed)
        {
            changed = false;
            for (auto& [bid, block] : func.blocks)
            {
                std::set<BE::Register> newOUT;
                for (auto* s : succs[block])
                {
                    auto it = IN.find(s);
                    if (it != IN.end()) newOUT.insert(it->second.begin(), it->second.end());
                }
                std::set<BE::Register> newIN = USE[block];

                for (auto& r : newOUT)
                    if (!DEF[block].count(r)) newIN.insert(r);

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
        // Step 5: Build live intervals
        // ============================================================================
        std::map<BE::Register, Interval> intervals;

        for (auto& [bid, block] : func.blocks)
        {
            auto [blockStart, blockEnd] = blockRange[block];

            // All registers in OUT are live at block end
            for (const auto& r : OUT[block])
            {
                if (!r.isVreg) continue;
                intervals[r].vreg = r;
                intervals[r].addSegment(blockStart, blockEnd);
            }

            // Walk instructions backward to build intervals
            int instIdx = blockEnd - 1;
            for (auto it = block->insts.rbegin(); it != block->insts.rend(); ++it, --instIdx)
            {
                std::vector<BE::Register> uses, defs;
                BE::Targeting::g_adapter->enumUses(*it, uses);
                BE::Targeting::g_adapter->enumDefs(*it, defs);

                // Definitions kill the live range at this point
                for (auto& d : defs)
                {
                    if (!d.isVreg) continue;
                    intervals[d].vreg = d;
                    // Definition point starts a new segment
                    intervals[d].addSegment(instIdx, instIdx + 1);
                }

                // Uses extend the live range
                for (auto& u : uses)
                {
                    if (!u.isVreg) continue;
                    intervals[u].vreg = u;
                    intervals[u].addSegment(blockStart, instIdx + 1);
                }
            }
        }

        // Merge segments in each interval
        for (auto& [vreg, interval] : intervals)
        {
            interval.merge();
        }

        // Mark intervals that cross call points
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
        // Step 6: Linear scan main loop
        // ============================================================================
        auto allIntRegs   = buildAllocatableInt(regInfo);
        auto allFloatRegs = buildAllocatableFloat(regInfo);

        // Separate intervals by type
        std::vector<Interval*> intIntervals, fpIntervals;
        for (auto& [vreg, interval] : intervals)
        {
            if (isIntegerType(vreg.dt))
                intIntervals.push_back(&interval);
            else if (isFloatType(vreg.dt))
                fpIntervals.push_back(&interval);
        }

        // Sort by start point
        std::sort(intIntervals.begin(), intIntervals.end(), IntervalOrder());
        std::sort(fpIntervals.begin(), fpIntervals.end(), IntervalOrder());

        // Callee-saved register sets for prioritization
        std::set<int> calleeSavedIntSet(regInfo.calleeSavedIntRegs().begin(), regInfo.calleeSavedIntRegs().end());
        std::set<int> calleeSavedFPSet(regInfo.calleeSavedFloatRegs().begin(), regInfo.calleeSavedFloatRegs().end());

        // Allocate integer registers
        auto allocateIntervals = [&](std::vector<Interval*>& toAlloc, const std::vector<int>& allocRegs,
                                     const std::set<int>& calleeSaved) {
            std::set<Interval*, ActiveOrder> active;
            std::set<int>                    freeRegs(allocRegs.begin(), allocRegs.end());

            auto spillInterval = [&](Interval* iv) {
                if (!iv) return;
                iv->assignedReg = -1;
                int spillWidth  = iv->vreg.dt ? iv->vreg.dt->getDataWidth() : 8;
                if (iv->spillSlot < 0) iv->spillSlot = func.frameInfo.createSpillSlot(spillWidth);
            };

            for (Interval* interval : toAlloc)
            {
                int start = interval->getStart();

                // Expire old intervals
                for (auto it = active.begin(); it != active.end();)
                {
                    if ((*it)->getEnd() <= start)
                    {
                        if ((*it)->assignedReg >= 0) freeRegs.insert((*it)->assignedReg);
                        it = active.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                // Try to find a free register
                int chosenReg = -1;

                if (interval->crossesCall)
                {
                    // Prefer callee-saved registers; if none available, force spill later
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
                    chosenReg = *freeRegs.begin();
                }

                if (chosenReg >= 0)
                {
                    interval->assignedReg = chosenReg;
                    freeRegs.erase(chosenReg);
                    active.insert(interval);
                }
                else
                {
                    // Need to spill: choose the interval with the furthest end point
                    Interval* toSpill = nullptr;
                    for (auto* act : active)
                    {
                        if (interval->crossesCall && !calleeSaved.count(act->assignedReg)) continue;
                        if (!toSpill || act->getEnd() > toSpill->getEnd()) toSpill = act;
                    }

                    bool canTakeReg = toSpill && (!interval->crossesCall || calleeSaved.count(toSpill->assignedReg));
                    if (canTakeReg && toSpill->getEnd() > interval->getEnd())
                    {
                        // Spill the active interval, give its register to current
                        interval->assignedReg = toSpill->assignedReg;
                        spillInterval(toSpill);
                        active.erase(toSpill);
                        active.insert(interval);
                    }
                    else
                    {
                        // Spill current interval
                        spillInterval(interval);
                    }
                }
            }
        };

        allocateIntervals(intIntervals, allIntRegs, calleeSavedIntSet);
        allocateIntervals(fpIntervals, allFloatRegs, calleeSavedFPSet);

        std::cerr << "[RA] " << func.name << " step7 rewrite" << std::endl;
        // ============================================================================
        // Step 7: Rewrite MIR (insert reload/spill, replace use/def)
        // ============================================================================
        // Build vreg -> assignment mapping
        std::map<BE::Register, std::pair<int, int>> vregToAssignment;  // vreg -> (physReg, spillSlot)
        for (auto& [vreg, interval] : intervals)
        {
            vregToAssignment[vreg] = {interval.assignedReg, interval.spillSlot};
        }
        if (func.name == "reverse")
        {
            std::cerr << "[RA] mapping for reverse:" << std::endl;
            for (auto& [vr, asg] : vregToAssignment)
            {
                std::cerr << "  v" << vr.rId << " isV=" << vr.isVreg << " dt=" << (vr.dt ? (vr.dt->dt == BE::DataType::Type::INT ? "I" : "F") : "?")
                          << " -> reg " << asg.first << " spill " << asg.second << std::endl;
            }
        }

        // Get scratch registers (prefer reserved registers that are not sp/ra/zero)
        std::vector<int> scratchIntPool;
        std::vector<int> scratchFloatPool;
        for (int r : regInfo.reservedRegs())
        {
            if (r >= 32)
                scratchFloatPool.push_back(r);
            else if (r != regInfo.spRegId() && r != regInfo.raRegId() && r != regInfo.zeroRegId() && r != 3 && r != 4 &&
                     r != 5)  // t0 is used by lowering for big offsets, avoid clobbering it
                scratchIntPool.push_back(r);
        }
        if (scratchIntPool.empty() && !allIntRegs.empty()) scratchIntPool.push_back(allIntRegs.back());
        if (scratchFloatPool.empty() && !allFloatRegs.empty()) scratchFloatPool.push_back(allFloatRegs.back());

        for (auto& [bid, block] : func.blocks)
        {
            for (size_t idx = 0; idx < block->insts.size(); ++idx)
            {
                auto* inst = block->insts[idx];

                std::vector<BE::Register> uses, defs;
                BE::Targeting::g_adapter->enumUses(inst, uses);
                BE::Targeting::g_adapter->enumDefs(inst, defs);

                std::vector<BE::Register>      physRegs;
                BE::Targeting::g_adapter->enumPhysRegs(inst, physRegs);
                std::set<int> busyPhys;
                for (auto& pr : physRegs) busyPhys.insert(pr.rId);

                std::vector<BE::MInstruction*> before, after;
                std::set<int>                   usedScratchInt, usedScratchFloat;
                std::vector<int>               useScratchIntList, useScratchFloatList;

                // Handle uses: replace vreg with physReg or insert reload
                for (const auto& u : uses)
                {
                    if (!u.isVreg) continue;
                    auto assignIt = vregToAssignment.find(u);
                    if (assignIt == vregToAssignment.end()) continue;

                    auto [physReg, spillSlot] = assignIt->second;
                    if (physReg >= 0)
                    {
                        BE::Register phys(physReg, u.dt, false);
                        BE::Targeting::g_adapter->replaceUse(inst, u, phys);
                    }
                    else if (spillSlot >= 0)
                    {
                        bool isFloat = isFloatType(u.dt);
                        auto& pool   = isFloat ? scratchFloatPool : scratchIntPool;
                        auto& used   = isFloat ? usedScratchFloat : usedScratchInt;
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
                            before.push_back(new BE::FILoadInst(scratchReg, spillSlot, "reload from spill slot"));
                            BE::Targeting::g_adapter->replaceUse(inst, u, scratchReg);
                            if (isFloat)
                                useScratchFloatList.push_back(scratch);
                            else
                                useScratchIntList.push_back(scratch);
                        }
                    }
                }

                // Handle defs: replace vreg with physReg or insert spill
                for (const auto& d : defs)
                {
                    if (!d.isVreg) continue;
                    auto assignIt = vregToAssignment.find(d);
                    if (assignIt == vregToAssignment.end()) continue;

                    auto [physReg, spillSlot] = assignIt->second;
                    if (physReg >= 0)
                    {
                        BE::Register phys(physReg, d.dt, false);
                        BE::Targeting::g_adapter->replaceDef(inst, d, phys);
                    }
                    else if (spillSlot >= 0)
                    {
                        bool isFloat = isFloatType(d.dt);
                        auto& pool   = isFloat ? scratchFloatPool : scratchIntPool;
                        auto& used   = isFloat ? usedScratchFloat : usedScratchInt;
                        int   scratch = -1;
                        for (int r : pool)
                        {
                            if (used.count(r)) continue;
                            scratch = r;
                            used.insert(r);
                            break;
                        }
                        if (scratch < 0)
                        {
                            if (!isFloat && !useScratchIntList.empty()) scratch = useScratchIntList.front();
                            if (isFloat && !useScratchFloatList.empty()) scratch = useScratchFloatList.front();
                        }
                        if (scratch >= 0)
                        {
                            BE::Register scratchReg(scratch, d.dt, false);
                            BE::Targeting::g_adapter->replaceDef(inst, d, scratchReg);
                            after.push_back(new BE::FIStoreInst(scratchReg, spillSlot, "spill to spill slot"));
                        }
                    }
                }

                if (!before.empty())
                {
                    block->insts.insert(block->insts.begin() + idx, before.begin(), before.end());
                    idx += before.size();
                }

                if (!after.empty())
                {
                    block->insts.insert(block->insts.begin() + idx + 1, after.begin(), after.end());
                }
            }
        }

        // Debug: check remaining vregs
        std::cerr << "[RA] function " << func.name << " end" << std::endl;
    }
}  // namespace BE::RA
