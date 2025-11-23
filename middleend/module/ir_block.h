#ifndef __MIDDLEEND_MODULE_IR_BLOCK_H__
#define __MIDDLEEND_MODULE_IR_BLOCK_H__

#include <middleend/module/ir_instruction.h>
#include <deque>

#define ENABLE_IRBLOCK_COMMENT

namespace ME
{
    class Block : public Visitable
    {
      public:
        std::deque<Instruction*> insts;    // 指令列表
        size_t                   blockId;  // 基本块编号

      public:
#ifndef ENABLE_IRBLOCK_COMMENT
        Block(size_t id = 0, const std::string& c = "") : blockId(id) {}
        void        setComment(const std::string& c) {}
        std::string getComment() const { return ""; }
#else
        std::string comment;
        Block(size_t id = 0, const std::string& c = "") : blockId(id), comment(c) {}
        void        setComment(const std::string& c) { comment = c; }
        std::string getComment() const
        {
            if (comment.empty()) return "";
            return " ; " + comment;
        }
#endif
        ~Block();

      public:
        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }

        void insertFront(Instruction* inst);
        void insertBack(Instruction* inst);
        void insert(Instruction* inst) { insertBack(inst); }
    };
}  // namespace ME

#endif  // __MIDDLEEND_MODULE_IR_BLOCK_H__
