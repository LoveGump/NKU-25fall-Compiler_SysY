#ifndef __BACKEND_DAG_SD_NODE_H__
#define __BACKEND_DAG_SD_NODE_H__

#include <backend/mir/m_defs.h>
#include <backend/dag/folding_set.h>
#include <backend/dag/isd.h>
#include <vector>
#include <cstdint>
#include <string>

namespace BE
{
    namespace DAG
    {
        class SDNode;

        // SDValue: 指向 DAG 节点的"智能引用"
        // 一个节点可能产生多个结果，res_no_ 指定是哪个结果
        class SDValue
        {
            SDNode*  node_;// 指向的节点
            uint32_t res_no_;//结果编号

          public:
            SDValue() : node_(nullptr), res_no_(0) {}
            SDValue(SDNode* node, uint32_t res_no) : node_(node), res_no_(res_no) {}

            SDNode*  getNode() const { return node_; }//获取节点
            uint32_t getResNo() const { return res_no_; }//获取结果编号
            explicit operator bool() const { return node_ != nullptr; }//是否为空
        };

        
        // SDNode: DAG 中的一个节点，代表一个操作
        class SDNode
        {
            uint32_t               id_ = 0;// 节点编号
            uint32_t               opcode_;// 操作码
            std::vector<SDValue>   operands_;// 操作数
            std::vector<DataType*> value_types_;// 结果类型

            bool        has_imm_i64_ = false;// 是否有整数立即数
            int64_t     imm_i64_     = 0;// 整数立即数
            bool        has_imm_f32_ = false;// 是否有浮点数立即数
            float       imm_f32_     = 0.0f;// 浮点数立即数
            bool        has_symbol_  = false;// 是否有符号
            std::string symbol_;//符号名

            bool   has_ir_reg_id_ = false;// 是否有 IR 寄存器 ID
            size_t ir_reg_id_     = 0;// IR 寄存器 ID

            bool has_frame_index_ = false;// 是否有栈帧索引
            int  frame_index_     = -1;// 栈帧索引

          public:
            SDNode(uint32_t opcode, const std::vector<DataType*>& vts, const std::vector<SDValue>& ops)
                : opcode_(opcode), operands_(ops), value_types_(vts)
            {}

            void     setId(uint32_t id) { id_ = id; }//设置id
            uint32_t getId() const { return id_; }//获取id

            uint32_t getOpcode() const { return opcode_; }//获取操作码
            void     setOpcode(uint32_t opc) { opcode_ = opc; }//设置操作码

            const std::vector<SDValue>& getOperands() const { return operands_; }//获取操作数
            const SDValue&              getOperand(unsigned i) const { return operands_[i]; }//获取第i个操作数
            void                        setOperand(unsigned i, const SDValue& v)//设置第i个操作数
            {
                if (i < operands_.size()) operands_[i] = v;
            }
            void replaceOperands(const std::vector<SDValue>& ops) { operands_ = ops; }//替换操作数

            unsigned getNumOperands() const { return operands_.size(); }//获取操作数数量
            unsigned getNumValues() const { return value_types_.size(); }//获取结果数量

            DataType* getValueType(unsigned i) const { return value_types_[i]; }//获取第i个结果类型

            void setImmI64(int64_t v)//设置64位整数立即数
            {
                has_imm_i64_ = true;
                imm_i64_     = v;
            }
            bool    hasImmI64() const { return has_imm_i64_; }//是否有64位整数立即数
            int64_t getImmI64() const { return imm_i64_; }//获取64位整数立即数

            void setImmF32(float v)//设置32位浮点数立即数
            {
                has_imm_f32_ = true;
                imm_f32_     = v;
            }
            bool  hasImmF32() const { return has_imm_f32_; }//是否有32位浮点数立即数
            float getImmF32() const { return imm_f32_; }//获取32位浮点数立即数

            void setSymbol(const std::string& s)//设置符号
            {
                has_symbol_ = true;
                symbol_     = s;
            }
            bool               hasSymbol() const { return has_symbol_; }//是否有符号
            const std::string& getSymbol() const { return symbol_; }//获取符号

            void setIRRegId(size_t id)//设置IR寄存器ID
            {
                has_ir_reg_id_ = true;
                ir_reg_id_     = id;
            }
            bool   hasIRRegId() const { return has_ir_reg_id_; }//是否有IR寄存器ID
            size_t getIRRegId() const { return ir_reg_id_; }//获取IR寄存器ID

            void setFrameIndex(int fi)//设置栈帧索引
            {
                has_frame_index_ = true;
                frame_index_     = fi;
            }
            bool hasFrameIndex() const { return has_frame_index_; }//是否有栈帧索引
            int  getFrameIndex() const { return frame_index_; }//获取栈帧索引

            //把节点的所有重要属性添加到 FoldingSetNodeID， DAG 节点生成一个唯一的标识（指纹/哈希），用于判断两个节点是否完全相同。
            void Profile(FoldingSetNodeID& ID) const//用于FoldingSet的Profile
            {
                // 1. 操作码（ADD? MUL? LOAD?）
                ID.AddInteger(opcode_);

                 // 2. 操作数和结果类型的数量
                ID.AddInteger(operands_.size());
                ID.AddInteger(value_types_.size());

                // 3. 每个操作数（哪个节点的第几个结果）
                for (const auto& op : operands_)
                {
                    ID.AddPointer(op.getNode());
                    ID.AddInteger(op.getResNo());
                }

                // 4. 每个结果类型
                for (auto* vt : value_types_) ID.AddPointer(vt);

                // 5. 可选属性：立即数、符号、栈帧索引等
                if (has_imm_i64_)
                {
                    ID.AddBoolean(true);
                    ID.AddInteger(imm_i64_);
                }
                else
                    ID.AddBoolean(false);

                if (has_imm_f32_)
                {
                    ID.AddBoolean(true);
                    ID.AddFloat(imm_f32_);
                }
                else
                    ID.AddBoolean(false);

                if (has_symbol_)
                {
                    ID.AddBoolean(true);
                    ID.AddString(symbol_);
                }
                else
                    ID.AddBoolean(false);

                if (has_frame_index_)
                {
                    ID.AddBoolean(true);
                    ID.AddInteger(frame_index_);
                }
                else
                    ID.AddBoolean(false);

                // 6. REG 节点特殊处理：加入 IR 寄存器 ID
                if (opcode_ == static_cast<unsigned>(ISD::REG))
                {
                    if (has_ir_reg_id_)
                    {
                        ID.AddBoolean(true);
                        ID.AddInteger(ir_reg_id_);
                    }
                    else
                        ID.AddBoolean(false);
                }
            }
        };

    }  // namespace DAG
}  // namespace BE

#endif  // __BACKEND_DAG_SD_NODE_H__