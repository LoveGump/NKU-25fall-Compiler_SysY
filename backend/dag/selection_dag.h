#ifndef __BACKEND_DAG_SELECTION_DAG_H__
#define __BACKEND_DAG_SELECTION_DAG_H__

#include <backend/dag/sd_node.h>
#include <backend/dag/folding_set.h>
#include <backend/dag/isd.h>
#include <vector>
#include <memory>
#include <unordered_map>

namespace BE
{
    namespace DAG
    {
        // SelectionDAG: 选择 DAG，用于表示目标无关的中间表示
        class SelectionDAG
        {
            std::vector<SDNode*>                          nodes_;// 节点列表
            uint32_t                                      next_id_ = 0;// 下一个节点 ID
            std::unordered_map<FoldingSetNodeID, SDNode*> folding_set_;// 节点折叠集合

          public:
            SelectionDAG() = default;
            ~SelectionDAG()
            {
                for (auto* n : nodes_)
                {
                    if (!n) continue;
                    delete n;
                    n = nullptr;
                }
            }

            // 获取节点，参数：
            // opcode: 操作码
            // vts: 结果类型列表
            // ops: 操作数列表
            SDValue getNode(uint32_t opcode, const std::vector<DataType*>& vts, const std::vector<SDValue>& ops)
            {
                SDNode temp(opcode, vts, ops);//建立临时节点

                FoldingSetNodeID ID;//节点 ID
                temp.Profile(ID);//计算节点 ID

                auto it = folding_set_.find(ID);//查找节点
                if (it != folding_set_.end()) return SDValue(it->second, 0);//如果找到，返回节点

                auto* n = new SDNode(opcode, vts, ops);//创建节点
                nodes_.push_back(n);
                n->setId(next_id_++);//设置节点 ID

                folding_set_[ID] = n;//将节点加入折叠集合

                //其余类型的节点与这个流程类似

                return SDValue(n, 0);
            }

            // 获取符号节点
            SDValue getSymNode(uint32_t opcode, const std::vector<DataType*>& vts, const std::vector<SDValue>& ops,
                const std::string& symbol)
            {
                SDNode temp(opcode, vts, ops);
                temp.setSymbol(symbol);

                FoldingSetNodeID ID;
                temp.Profile(ID);

                auto it = folding_set_.find(ID);
                if (it != folding_set_.end()) return SDValue(it->second, 0);

                auto* n = new SDNode(opcode, vts, ops);
                n->setSymbol(symbol);//设置符号
                nodes_.push_back(n);
                n->setId(next_id_++);//设置节点 ID

                folding_set_[ID] = n;//将节点加入折叠集合

                return SDValue(n, 0);
            }

            // 获取立即数节点
            SDValue getImmNode(
                uint32_t opcode, const std::vector<DataType*>& vts, const std::vector<SDValue>& ops, int64_t imm)
            {
                SDNode temp(opcode, vts, ops);
                temp.setImmI64(imm);

                FoldingSetNodeID ID;
                temp.Profile(ID);

                auto it = folding_set_.find(ID);
                if (it != folding_set_.end()) return SDValue(it->second, 0);

                auto* n = new SDNode(opcode, vts, ops);
                n->setImmI64(imm);
                nodes_.push_back(n);
                n->setId(next_id_++);

                folding_set_[ID] = n;

                return SDValue(n, 0);
            }

            // 获取栈帧索引节点
            SDValue getFrameIndexNode(int frame_index, DataType* ptr_ty)
            {
                SDNode temp(static_cast<unsigned>(ISD::FRAME_INDEX), {ptr_ty}, {});
                temp.setFrameIndex(frame_index);

                FoldingSetNodeID ID;
                temp.Profile(ID);

                auto it = folding_set_.find(ID);
                if (it != folding_set_.end()) return SDValue(it->second, 0);

                auto* n = new SDNode(static_cast<unsigned>(ISD::FRAME_INDEX), {ptr_ty}, {});
                n->setFrameIndex(frame_index);
                nodes_.push_back(n);
                n->setId(next_id_++);

                folding_set_[ID] = n;

                return SDValue(n, 0);
            }

            // 获取寄存器节点
            SDValue getRegNode(size_t ir_reg_id, DataType* vt)
            {
                SDNode temp(static_cast<unsigned>(ISD::REG), {vt}, {});
                temp.setIRRegId(ir_reg_id);

                FoldingSetNodeID ID;
                temp.Profile(ID);

                auto it = folding_set_.find(ID);
                if (it != folding_set_.end()) return SDValue(it->second, 0);

                auto* n = new SDNode(static_cast<unsigned>(ISD::REG), {vt}, {});
                n->setIRRegId(ir_reg_id);
                nodes_.push_back(n);
                n->setId(next_id_++);

                folding_set_[ID] = n;

                return SDValue(n, 0);
            }

            // 获取常量节点
            SDValue getConstantI64(int64_t value, DataType* vt)
            {
                SDNode temp(static_cast<unsigned>(ISD::CONST_I64), {vt}, {});
                temp.setImmI64(value);

                FoldingSetNodeID ID;
                temp.Profile(ID);

                auto it = folding_set_.find(ID);
                if (it != folding_set_.end()) { return SDValue(it->second, 0); }

                auto* n = new SDNode(static_cast<unsigned>(ISD::CONST_I64), {vt}, {});
                n->setImmI64(value);
                nodes_.push_back(n);
                n->setId(next_id_++);
                folding_set_[ID] = n;

                return SDValue(n, 0);
            }

            // 获取常量节点
            SDValue getConstantF32(float value, DataType* vt)
            {
                SDNode temp(static_cast<unsigned>(ISD::CONST_F32), {vt}, {});
                temp.setImmF32(value);

                FoldingSetNodeID ID;
                temp.Profile(ID);

                auto it = folding_set_.find(ID);
                if (it != folding_set_.end()) { return SDValue(it->second, 0); }

                auto* n = new SDNode(static_cast<unsigned>(ISD::CONST_F32), {vt}, {});
                n->setImmF32(value);
                nodes_.push_back(n);
                n->setId(next_id_++);
                folding_set_[ID] = n;

                return SDValue(n, 0);
            }

            const std::vector<SDNode*>& getNodes() const { return nodes_; }
        };

    }  // namespace DAG
}  // namespace BE

#endif  // __BACKEND_DAG_SELECTION_DAG_H__
