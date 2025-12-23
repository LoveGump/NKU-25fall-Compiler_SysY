#ifndef __MIDDLEEND_PASS_SCCP_H__
#define __MIDDLEEND_PASS_SCCP_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>
#include <deque>
#include <map>
#include <set>
#include <vector>
#include <utility>

namespace ME
{
    class SCCPEvalVisitor;
    class SCCPReplaceVisitor;
    class SCCPBranchSimplifyVisitor;

    // 稀疏条件常量传播（SCCP），用可达性与格值信息做常量传播与分支折叠
    class SCCPPass : public FunctionPass
    {
      public:
        SCCPPass()  = default;
        ~SCCPPass() = default;

        void runOnFunction(Function& function) override;

      private:
        // 格值三态：未知、常量、过定义
        enum class LatticeKind
        {
            UNDEF,
            CONST,
            OVERDEFINED
        };

        // 格值承载具体常量（i32/f32）或状态
        struct LatticeVal
        {
            LatticeKind kind = LatticeKind::UNDEF;
            DataType    type = DataType::UNK;
            int         i32  = 0;
            float       f32  = 0.0f;
        };

        friend class SCCPEvalVisitor;
        friend class SCCPReplaceVisitor;

      private:
        Function* currFunc = nullptr;  // 当前函数上下文

        std::map<size_t, LatticeVal>                     valueMap;       // 寄存器 -> 格值
        std::map<size_t, std::vector<Instruction*>>      userMap;        // 寄存器 -> 使用者指令
        std::map<Instruction*, Block*>                   instBlockMap;   // 指令 -> 基本块
        std::set<size_t>                                 reachableBlocks; // 可达基本块集合
        std::set<std::pair<size_t, size_t>>              reachableEdges;  // 可达边集合
        std::deque<Block*>                               blockWorklist;   // 基本块工作队列
        std::deque<Instruction*>                         instWorklist;    // 指令工作队列

      private:
        void   initialize(Function& function);

        void removePhiIncoming(Block* block, Operand* label);
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_SCCP_H__
