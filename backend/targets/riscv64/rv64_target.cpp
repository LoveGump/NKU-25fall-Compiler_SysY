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
        runPreRAPasses(*backend, &s_adapter);
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
