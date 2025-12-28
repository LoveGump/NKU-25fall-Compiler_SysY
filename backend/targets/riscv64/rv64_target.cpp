#include <backend/targets/riscv64/rv64_target.h>
#include <backend/target/registry.h>

#include <backend/targets/riscv64/isel/rv64_dag_isel.h>
#include <backend/targets/riscv64/passes/lowering/frame_lowering.h>
#include <backend/targets/riscv64/passes/lowering/stack_lowering.h>
#include <backend/targets/riscv64/passes/lowering/phi_elimination.h>
#include <backend/targets/riscv64/rv64_codegen.h>

#include <backend/common/cfg_builder.h>
#include <backend/ra/linear_scan.h>
#include <backend/targets/riscv64/rv64_reg_info.h>
#include <backend/targets/riscv64/rv64_instr_adapter.h>
#include <backend/dag/dag_builder.h>
#include <backend/targets/riscv64/dag/rv64_dag_legalize.h>

#include <debug.h>

#include <map>
#include <iostream>
#include <vector>
namespace BE
{
    namespace RA
    {
        void setTargetInstrAdapter(const BE::Targeting::TargetInstrAdapter* adapter);
    }
}  // namespace BE

namespace BE::Targeting::RV64
{
    namespace
    {
        struct AutoRegister
        {
            AutoRegister()
            {
                BE::Targeting::TargetRegistry::registerTargetFactory("riscv64", []() { return new Target(); });
                BE::Targeting::TargetRegistry::registerTargetFactory("riscv", []() { return new Target(); });
                BE::Targeting::TargetRegistry::registerTargetFactory("rv64", []() { return new Target(); });
            }
        } s_auto_register;
    }  // namespace

    namespace
    {
        static void runPreRAPasses(BE::Module& m, const BE::Targeting::TargetInstrAdapter* adapter)
        {
            // 对实现了 mem2reg 优化的同学，还需完成 Phi Elimination
            BE::RV64::Passes::Lowering::PhiEliminationPass phiElim;
            phiElim.runOnModule(m, adapter);
        }
        static void dumpModule(const char* tag, BE::Module& m)
        {
            std::cerr << "[MIR Dump] " << tag << std::endl;
            for (auto* func : m.functions)
            {
                if (!func) continue;
                std::cerr << "Function " << func->name << std::endl;
                for (auto& [bid, block] : func->blocks)
                {
                    if (!block) continue;
                    std::cerr << "  Block " << bid << std::endl;
                    for (auto* inst : block->insts)
                    {
                        if (auto* phi = dynamic_cast<BE::PhiInst*>(inst))
                        {
                            std::cerr << "    phi v" << phi->resReg.rId << " <-";
                            for (auto& [lbl, op] : phi->incomingVals)
                            {
                                std::cerr << " [" << lbl << ":";
                                if (auto* rop = dynamic_cast<BE::RegOperand*>(op))
                                    std::cerr << "v" << rop->reg.rId;
                                else if (auto* iop = dynamic_cast<BE::I32Operand*>(op))
                                    std::cerr << iop->val;
                                else
                                    std::cerr << "?";
                                std::cerr << "]";
                            }
                            std::cerr << std::endl;
                        }
                        else if (auto* mv = dynamic_cast<BE::MoveInst*>(inst))
                        {
                            std::cerr << "    move ";
                            if (auto* d = dynamic_cast<BE::RegOperand*>(mv->dest)) std::cerr << "v" << d->reg.rId;
                            std::cerr << " <- ";
                            if (auto* s = dynamic_cast<BE::RegOperand*>(mv->src))
                                std::cerr << "v" << s->reg.rId;
                            else if (auto* i = dynamic_cast<BE::I32Operand*>(mv->src))
                                std::cerr << i->val;
                            std::cerr << std::endl;
                        }
                        else if (auto* ri = dynamic_cast<BE::RV64::Instr*>(inst))
                        {
                            std::cerr << "    op" << static_cast<int>(ri->op);
                            if (ri->rd.isVreg) std::cerr << " v" << ri->rd.rId;
                            else if (ri->rd.rId) std::cerr << " x" << ri->rd.rId;
                            if (ri->rs1.isVreg) std::cerr << " rs1=v" << ri->rs1.rId;
                            else if (ri->rs1.rId) std::cerr << " rs1=x" << ri->rs1.rId;
                            if (ri->rs2.isVreg) std::cerr << " rs2=v" << ri->rs2.rId;
                            else if (ri->rs2.rId) std::cerr << " rs2=x" << ri->rs2.rId;
                            if (ri->use_label) std::cerr << " ->" << ri->label.jmp_label;
                            std::cerr << std::endl;
                        }
                        else
                        {
                            std::cerr << "    [inst]" << std::endl;
                        }
                    }
                }
            }
        }
        static void runRAPipeline(BE::Module& m, const BE::Targeting::RV64::RegInfo& regInfo)
        {
            BE::RA::LinearScanRA ls;
            ls.allocate(m, regInfo);
        }
        static void runPostRAPasses(BE::Module& m)
        {
            BE::RV64::Passes::Lowering::FrameLoweringPass frameLowering;
            frameLowering.runOnModule(m);

            std::cerr << "[RV64] stack lowering" << std::endl;
            BE::RV64::Passes::Lowering::StackLoweringPass stackLowering;
            stackLowering.runOnModule(m);
            std::cerr << "[RV64] stack lowering done" << std::endl;
        }
    }  // namespace

    void Target::runPipeline(ME::Module* ir, BE::Module* backend, std::ostream* out)
    {
        static BE::Targeting::RV64::InstrAdapter s_adapter;
        static BE::Targeting::RV64::RegInfo      s_regInfo;
        BE::Targeting::setTargetInstrAdapter(&s_adapter);

        std::cerr << "[RV64] DAG isel start" << std::endl;
        BE::RV64::DAGIsel isel(ir, backend, this);
        // BE::RV64::IRIsel isel(ir, backend, this);
        isel.run();
        std::cerr << "[RV64] DAG isel done" << std::endl;

        std::cerr << "[RV64] pre-RA passes" << std::endl;
        dumpModule("after isel", *backend);
        runPreRAPasses(*backend, &s_adapter);
        dumpModule("after phi elim", *backend);
        std::cerr << "[RV64] RA" << std::endl;
        runRAPipeline(*backend, s_regInfo);
        std::cerr << "[RV64] post-RA passes" << std::endl;
        runPostRAPasses(*backend);

        std::cerr << "[RV64] codegen" << std::endl;
        BE::RV64::CodeGen codegen(backend, *out);
        codegen.generateAssembly();
        std::cerr << "[RV64] done" << std::endl;
    }
}  // namespace BE::Targeting::RV64
