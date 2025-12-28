#ifndef __BACKEND_TARGETS_RISCV64_RV64_REG_INFO_H__
#define __BACKEND_TARGETS_RISCV64_RV64_REG_INFO_H__

#include <backend/target/target_reg_info.h>
#include <backend/targets/riscv64/rv64_defs.h>
#include <map>

namespace BE::Targeting::RV64
{
    class RegInfo : public TargetRegInfo
    {
      private:
        // RISC-V64 register IDs based on RV64_REGS macro ordering:
        // x0=0, x1(ra)=1, x2(sp)=2, x3(gp)=3, x4(tp)=4, x5(t0)=5, ...
        // x10(a0)=10, ..., x17(a7)=17
        // x8(s0/fp)=8, x9(s1)=9, x18(s2)=18, ..., x27(s11)=27
        // f0=32, f1=33, ..., f31=63
        // f10(fa0)=42, ..., f17(fa7)=49
        // f8(fs0)=40, f9(fs1)=41, f18(fs2)=50, ..., f27(fs11)=59

        inline static const std::vector<int> intArgRegs_     = {10, 11, 12, 13, 14, 15, 16, 17};  // a0-a7
        inline static const std::vector<int> floatArgRegs_   = {42, 43, 44, 45, 46, 47, 48, 49};  // fa0-fa7
        inline static const std::vector<int> calleeSavedInt_ = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};  // s0-s11
        inline static const std::vector<int> calleeSavedFP_  = {40, 41, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59};  // fs0-fs11
        inline static const std::vector<int> reservedRegs_   = {0, 1, 2, 3, 4, 5, 6, 7, 32, 33, 34};  // x0, ra, sp, gp, tp, t0, t1, t2, ft0-2
        inline static const std::vector<int> intRegs_ = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
        };  // x0-x31
        inline static const std::vector<int> floatRegs_ = {
            32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
            48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63
        };  // f0-f31

      public:
        RegInfo() = default;

        int spRegId() const override { return 2; }    // x2 = sp
        int raRegId() const override { return 1; }    // x1 = ra
        int zeroRegId() const override { return 0; }  // x0 = zero

        const std::vector<int>& intArgRegs() const override { return intArgRegs_; }
        const std::vector<int>& floatArgRegs() const override { return floatArgRegs_; }

        const std::vector<int>& calleeSavedIntRegs() const override { return calleeSavedInt_; }
        const std::vector<int>& calleeSavedFloatRegs() const override { return calleeSavedFP_; }

        const std::vector<int>& reservedRegs() const override { return reservedRegs_; }
        const std::vector<int>& intRegs() const override { return intRegs_; }
        const std::vector<int>& floatRegs() const override { return floatRegs_; }
    };
}  // namespace BE::Targeting::RV64

#endif  // __BACKEND_TARGETS_RISCV64_RV64_REG_INFO_H__
