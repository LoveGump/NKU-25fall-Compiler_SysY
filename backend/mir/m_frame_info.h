#ifndef __BACKEND_MIR_M_FRAME_INFO_H__
#define __BACKEND_MIR_M_FRAME_INFO_H__

#include <unordered_map>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace BE
{
    /**
     * @brief 机器指令帧信息管理类
     * 负责维护函数栈帧布局，包括局部变量、溢出槽和参数传递区域。
     * 影响：计算最终栈大小及各变量相对于 SP 的偏移量，供指令选择和序言/尾声生成使用。
     */
    class MFrameInfo
    {
      public:
        /**
         * @brief 栈对象类型
         */
        enum class ObjectKind
        {
            LocalVar,   // 局部变量（对应 LLVM IR 中的 alloca）
            SpillSlot,  // 寄存器分配阶段产生的溢出槽
            OutArg      // 用于调用其他函数时的传参区域
        };

        /**
         * @brief 栈对象属性描述
         */
        struct FrameObject
        {
            int        size      = 0;   // 对象占据的字节大小
            int        alignment = 16;  // 对齐要求
            int        offset    = -1;  // 相对于栈指针的偏移量（初始化为-1表示未分配）
            ObjectKind kind      = ObjectKind::LocalVar; // 对象种类
        };

      private:
        /**
         * @brief IR 寄存器 ID 到栈对象的映射
         * 用于存储 alloca 指令产生的局部变量。
         */
        std::unordered_map<size_t, FrameObject> irRegToObject_;

        /**
         * @brief 溢出槽列表
         * 存储寄存器分配器产生的临时溢出空间，通过索引访问。
         */
        std::vector<FrameObject>                spillSlots_;

        /**
         * @brief 传参区域大小（最大值）
         * 记录当前函数作为调用者时，在栈上分配的传参空间大小。
         */
        int                                     paramSize_ = 0;

        /**
         * @brief 栈帧基准对齐要求
         * 确保整个栈帧的大小符合目标架构的对齐约束。
         */
        int                                     baseAlign_ = 16;

        /**
         * @brief 栈帧基准偏移量
         * 用于处理如被调用者保存寄存器（callee-saved）区域带来的额外偏移。
         */
        int                                     baseOffset_ = 0;

        /**
         * @brief 辅助函数：将数值 v 向上对齐到 a
         */
        static inline int alignTo(int v, int a) { return (v + (a - 1)) & ~(a - 1); }

      public:
        MFrameInfo() = default;

        /**
         * @brief 清空所有栈帧信息
         * 用于重置状态，通常在处理新函数前调用。
         */
        void clear()
        {
            irRegToObject_.clear();
            spillSlots_.clear();
            paramSize_ = 0;
        }

        /**
         * @brief 创建局部变量对象
         * 将 IR 中的 alloca 映射到栈空间描述中。
         */
        void createLocalObject(size_t irRegId, int sizeBytes, int alignment = 16)
        {
            irRegToObject_[irRegId] = FrameObject{sizeBytes, std::max(16, alignment), -1, ObjectKind::LocalVar};
        }

        /**
         * @brief 创建溢出槽
         * 供寄存器分配器使用，返回溢出槽的索引（FI）。
         */
        int createSpillSlot(int sizeBytes, int alignment = 8)
        {
            int fi = static_cast<int>(spillSlots_.size());
            spillSlots_.push_back(FrameObject{sizeBytes, std::max(8, alignment), -1, ObjectKind::SpillSlot});
            return fi;
        }

        /**
         * @brief 获取局部变量相对于 SP 的最终偏移
         * 影响：生成的 Load/Store 指令中的立即数偏移。
         */
        int getObjectOffset(size_t irRegId) const
        {
            auto it = irRegToObject_.find(irRegId);
            if (it == irRegToObject_.end()) return -1;
            return it->second.offset < 0 ? -1 : it->second.offset + baseOffset_;
        }

        /**
         * @brief 获取溢出槽相对于 SP 的最终偏移
         * 影响：溢出恢复（Reload）和溢出存储（Spill）指令。
         */
        int getSpillSlotOffset(int fi) const
        {
            if (fi < 0 || fi >= static_cast<int>(spillSlots_.size())) return -1;
            return spillSlots_[fi].offset < 0 ? -1 : spillSlots_[fi].offset + baseOffset_;
        }

        /**
         * @brief 检查是否存在指定的局部变量
         */
        bool hasObject(size_t irRegId) const { return irRegToObject_.find(irRegId) != irRegToObject_.end(); }

        /**
         * @brief 设置/获取参数区域大小
         * 影响：栈帧的总布局和 SP 的初始调整。
         */
        void setParamAreaSize(int bytes) { paramSize_ = std::max(paramSize_, alignTo(bytes, 16)); }
        int  getParamAreaSize() const { return paramSize_; }

        /**
         * @brief 设置/获取基准对齐和偏移
         */
        void setBaseAlignment(int a) { baseAlign_ = std::max(8, a); }
        int  getBaseAlignment() const { return baseAlign_; }
        void setBaseOffset(int off) { baseOffset_ = off; }
        int  getBaseOffset() const { return baseOffset_; }

        /**
         * @brief 计算所有栈对象的具体偏移量
         * 按照 传参区 -> 局部变量 -> 溢出槽 的顺序进行布局。
         * 影响：确定所有栈上数据的物理位置。
         */
        int calculateOffsets()
        {
            // 1. 初始化偏移量，从参数区域末尾开始分配
            int currentOffset = paramSize_;

            // 2. 布局局部变量：根据每个对象的对齐要求计算偏移并更新当前位置
            for (auto& [irRegId, obj] : irRegToObject_)
            {
                currentOffset = alignTo(currentOffset, obj.alignment);
                obj.offset    = currentOffset;
                currentOffset += obj.size;
            }

            // 3. 布局溢出槽：在局部变量之后分配溢出槽空间
            for (auto& slot : spillSlots_)
            {
                currentOffset = alignTo(currentOffset, slot.alignment);
                slot.offset   = currentOffset;
                currentOffset += slot.size;
            }

            // 4. 最后按栈帧基准对齐要求（如 16 字节）进行向上对齐
            return alignTo(currentOffset, baseAlign_);
        }

        /**
         * @brief 计算所需的总栈帧大小
         * 影响：函数序言中 sub sp, #size 指令的操作数。
         */
        int getStackSize() const
        {
            int maxOff = paramSize_;
            for (auto& [_, obj] : irRegToObject_)
                if (obj.offset >= 0) maxOff = std::max(maxOff, obj.offset + obj.size);
            for (auto& slot : spillSlots_)
                if (slot.offset >= 0) maxOff = std::max(maxOff, slot.offset + slot.size);
            return alignTo(maxOff, baseAlign_) + baseOffset_;
        }

        /**
         * @brief 获取或创建局部变量并立即分配偏移
         * 用于在后端按需动态生成栈空间。
         */
        int createOrGetObject(size_t irRegId, int sizeBytes, int alignment = 16)
        {
            // 1. 检查该寄存器是否已经分配了栈偏移，如果是则直接返回
            auto it = irRegToObject_.find(irRegId);
            if (it != irRegToObject_.end() && it->second.offset >= 0) return it->second.offset;

            // 2. 确保对象已创建（如果不存在则创建）
            if (it == irRegToObject_.end()) createLocalObject(irRegId, sizeBytes, alignment);

            // 3. 寻找当前栈中所有已分配对象的最大末尾边界，作为新分配的起点
            int currentOffset = paramSize_;
            for (const auto& [id, obj] : irRegToObject_)
            {
                if (obj.offset >= 0) currentOffset = std::max(currentOffset, obj.offset + obj.size);
            }

            // 4. 按照对齐要求计算新对象的起始偏移并记录
            currentOffset                  = alignTo(currentOffset, alignment);
            irRegToObject_[irRegId].offset = currentOffset;
            return currentOffset;
        }
    };
}  // namespace BE

#endif  // __BACKEND_MIR_M_FRAME_INFO_H__
