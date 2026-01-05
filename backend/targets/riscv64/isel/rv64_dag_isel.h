#ifndef __BACKEND_TARGETS_RISCV64_ISEL_RV64_DAG_ISEL_H__
#define __BACKEND_TARGETS_RISCV64_ISEL_RV64_DAG_ISEL_H__

#include <backend/isel/isel_base.h>
#include <backend/dag/selection_dag.h>
#include <middleend/module/ir_module.h>
#include <map>
#include <set>

/*
 * 注：当前目录下有 rv64_dag_isel 与 rv64_ir_isel 两份实现，它们的功能是一致的，你只需要选择其中一份来完成就行
 *
 * dag_isel 基于选择 DAG 来完成指令选择，你需要实现 DAG 的构建后再生成目标指令，总体难度可能更大，但 dag isel
 * 部分（不含 dag 部分）的代码量会更少一些
 *
 * ir_isel 则是直接基于 IR 来完成指令选择， 你只需要遍历 IR，然后根据 IR
 * 生成目标指令即可，总体难度可能更小一些，但代码量会更多一些
 *
 * 额外的，如果你正确实现了 dag 的构建，那么你可以在其它 Target （如 aarch64）中直接复用构建好的 dag，从而减少重复劳动
 */



namespace BE::Targeting
{
    class BackendTarget;
}

namespace BE::RV64
{
    class DAGIsel : public BE::ISelBase<DAGIsel>
    {
        friend class BE::ISelBase<DAGIsel>;

      public:
        DAGIsel(ME::Module* ir_module, BE::Module* backend_module, BE::Targeting::BackendTarget* target)
            : BE::ISelBase<DAGIsel>(backend_module), ir_module_(ir_module), target_(target)
        {}

      private:
        ME::Module*                   ir_module_;
        BE::Targeting::BackendTarget* target_;

        /**
         * @brief 每个函数级别的上下文信息
         *
         * 为什么需要函数级别的状态：
         * - vregMap：跨基本块的虚拟寄存器映射（PHI 节点需要）
         * - allocaFI：栈槽分配信息（在函数入口收集，多个块共享）
         */
        struct FunctionContext
        {
            BE::Function*              mfunc = nullptr;
            std::map<size_t, Register> vregMap;   ///< IR 寄存器 ID -> 后端虚拟寄存器
            std::map<size_t, int>      allocaFI;  ///< IR alloca 寄存器 ID -> 栈帧索引
        };

        FunctionContext ctx_;

        /**
         * @brief 每个基本块级别的状态（每个块重置）
         *
         * 为什么需要块级别的状态：
         * - nodeToVReg_：DAG 节点到其结果寄存器的映射（仅在块内有效）
         * - selected_：已选择的节点集合（防止重复选择）
         */
        std::map<const DAG::SDNode*, Register> nodeToVReg_;  ///< DAG 节点 -> 其结果虚拟寄存器
        std::set<const DAG::SDNode*>           selected_;    ///< 已经选择过的节点集合

        void runImpl();//入口
        void importGlobals();//导入全局变量
        void selectFunction(ME::Function* ir_func);//选择函数

        void collectAllocas(ME::Function* ir_func);//收集 alloca
        void setupParameters(ME::Function* ir_func);//设置参数
        void selectBlock(ME::Block* ir_block, const DAG::SelectionDAG& dag);//选择基本块

        // ==================== 阶段 1：调度（Schedule） ====================

        std::vector<const DAG::SDNode*> scheduleDAG(const DAG::SelectionDAG& dag);//调度DAG
        void                            allocateRegistersForNode(const DAG::SDNode* node);//分配寄存器

        // ==================== 阶段 2：选择（Select） ====================

        void     selectNode(const DAG::SDNode* node, BE::Block* m_block);//选择节点
        Register getOperandReg(const DAG::SDNode* node, BE::Block* m_block);//获取操作数寄存器
        Register materializeAddress(const DAG::SDNode* node, BE::Block* m_block);// materializeAddress
        bool     selectAddress(const DAG::SDNode* addrNode, const DAG::SDNode*& baseNode, int64_t& offset);//选择地址
        Register getOrCreateVReg(size_t ir_reg_id, BE::DataType* dt);//获取或创建虚拟寄存器

        void selectCopy(const DAG::SDNode* node, BE::Block* m_block);//选择copy
        void selectPhi(const DAG::SDNode* node, BE::Block* m_block);//选择phi
        void selectBinary(const DAG::SDNode* node, BE::Block* m_block);//选择二元操作
        void selectUnary(const DAG::SDNode* node, BE::Block* m_block);//选择一元操作
        void selectLoad(const DAG::SDNode* node, BE::Block* m_block);//选择load
        void selectStore(const DAG::SDNode* node, BE::Block* m_block);//选择store
        void selectICmp(const DAG::SDNode* node, BE::Block* m_block);//选择icmp
        void selectFCmp(const DAG::SDNode* node, BE::Block* m_block);//选择fcmp
        void selectBranch(const DAG::SDNode* node, BE::Block* m_block);//选择branch
        void selectCall(const DAG::SDNode* node, BE::Block* m_block);//选择call
        void selectRet(const DAG::SDNode* node, BE::Block* m_block);//选择ret
        void selectCast(const DAG::SDNode* node, BE::Block* m_block);//选择cast

        int dataTypeSize(BE::DataType* dt);//数据类型宽度

        //后续遍历辅助函数
        static void postOrderHelper(const DAG::SDNode* node,
                                    std::set<const DAG::SDNode*>& visited,
                                    std::vector<const DAG::SDNode*>& result);
    };

}  // namespace BE::RV64

#endif  // __BACKEND_TARGETS_RISCV64_ISEL_RV64_DAG_ISEL_H__
