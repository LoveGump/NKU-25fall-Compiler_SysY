#ifndef __FRONTEND_SYMBOL_SYMBOL_ENTRY_H__
#define __FRONTEND_SYMBOL_SYMBOL_ENTRY_H__

#include <string>
#include <unordered_map>

/*
 * ============================================================================
 * 符号表项的定义和实现
 *
 * 设计模式:
 *   1. 享元模式(Flyweight Pattern): 共享相同名字的Entry对象，节省内存
 *   2. 工厂模式(Factory Pattern): 通过getEntry()统一创建Entry对象
 *   3. 单例模式(Singleton Pattern): EntryDeleter确保只有一个实例管理资源清理
 *
 * 核心思想:
 *   - 对于同一个标识符名字(如变量名"x")，无论在代码中出现多少次，都共享同一个Entry对象
 *   - 通过entryMap保证名字到Entry对象的唯一映射
 *   - 使用RAII机制自动管理Entry对象的生命周期
 *
 * Lab2阶段:
 *   - Entry只存储符号名字，作为符号表的"键"
 *   - Lab3会扩展Entry，添加类型信息、作用域信息等
 * ============================================================================
 */

// ==================== Entry类: 符号表项 ====================
//
// 功能: 表示源代码中的一个符号(标识符)
//       如: 变量名、函数名、参数名等
//
// 特点:
//   1. 私有构造函数 - 外部不能直接new Entry
//   2. 静态工厂方法 - 只能通过getEntry()获取
//   3. 全局唯一性 - 相同名字的符号共享同一个Entry对象

// EntryDeleter需要访问private的clear()方法来清理资源，所以需要声明为友元类

// entryMap: 符号名到Entry对象的映射表（全局唯一）
// 作用1: 符号去重 - 保证相同名字只有一个Entry对象
// 作用2: 快速查找 - O(1)时间复杂度通过名字找到Entry
//
// 示例: 如果代码中有多处使用变量"x"
//       int x = 1;
//       x = x + 2;
//       func(x);
// 这三处的"x"都指向entryMap["x"]这个唯一的Entry对象

// clear(): 清理所有Entry对象，释放内存
// 私有方法，只能由EntryDeleter调用
// 在程序结束时自动调用（通过EntryDeleter的析构函数）

// getEntry(): 符号表项的唯一入口（工厂方法）
//
// 参数: name - 符号的名字（如变量名"x"，函数名"main"）
// 返回: Entry* - 对应名字的符号表项指针
//
// 工作原理:
//   1. 如果entryMap中已存在该名字 -> 直接返回已有的Entry
//   2. 如果entryMap中不存在 -> 创建新Entry并存入map，再返回
//
// 为什么使用静态方法:
//   1. 不需要Entry对象就能调用: Entry::getEntry("x")
//   2. 维护全局唯一的entryMap，不依赖于特定的Entry实例
//   3. 实现工厂模式，控制对象的创建过程
//
// 使用示例:
//   Entry* e1 = Entry::getEntry("x");  // 第一次，创建新Entry
//   Entry* e2 = Entry::getEntry("x");  // 第二次，返回已有Entry
//   assert(e1 == e2);  // e1和e2指向同一个对象！

// -------- 私有构造/析构：禁止外部创建 --------
// name: 符号的名字（如变量名"x"，函数名"main"）
// 构造函数私有化的目的:
//   1. 外部不能直接new Entry("x")
//   2. 强制使用getEntry()统一创建
//   3. 保证符号的全局唯一性
// getName(): 获取符号的名字
// 返回const引用，避免拷贝，且外部不能修改

// ==================== EntryDeleter类: 资源管理器 ====================
//
// 功能: 使用RAII机制自动管理Entry对象的生命周期
//
// RAII (Resource Acquisition Is Initialization):
//   - 资源获取即初始化
//   - 对象创建时获取资源，对象销毁时释放资源
//   - 利用C++的析构函数自动调用特性
//
// 工作原理:
//   1. 程序启动时: EntryDeleter的静态实例被创建（构造函数调用）
//   2. 程序运行中: Entry对象通过getEntry()不断创建并存入entryMap
//   3. 程序结束时: EntryDeleter的静态实例被销毁（析构函数自动调用）
//      -> 析构函数调用Entry::clear()
//      -> 清理所有Entry对象，释放内存
//
// 优点:
//   1. 自动化: 不需要手动清理，避免内存泄漏
//   2. 异常安全: 即使程序异常退出，析构函数也会被调用
//   3. 单一职责: Entry负责符号管理，EntryDeleter负责资源清理

// getInstance(): 获取EntryDeleter的唯一实例（单例模式）
//
// 实现细节（在.cpp中）:
//   static EntryDeleter instance;  // 局部静态变量
//   return instance;
//
// C++11保证:
//   - 局部静态变量的初始化是线程安全的
//   - 只会创建一次
//   - 程序结束时自动销毁（调用析构函数）
// 删除拷贝构造函数和赋值运算符
// 原因: EntryDeleter是单例，不应该被复制
//       如果允许复制，会导致多次调用析构函数，重复释放资源
// 析构函数: 程序结束时自动调用Entry::clear()
// 这是整个资源管理机制的核心
// 单例模式实现

// 私有构造/析构：外部不能创建EntryDeleter对象

namespace FE::Sym
{
    // 符号表中的一个符号表项
    class Entry
    {
        friend class EntryDeleter;

      private:
        static std::unordered_map<std::string, Entry*> entryMap;  // 静态成员变量，所有Entry对象共享
        static void                                    clear();

      public:
        static Entry* getEntry(std::string name);  // 创建或获取唯一的Entry对象

      private:
        Entry(std::string name = "NULL");
        ~Entry() = default;
        std::string name;

      public:
        const std::string& getName();
    };

    // 负责管理Entry对象生命周期的单例类
    class EntryDeleter
    {
      private:
        EntryDeleter();
        ~EntryDeleter();

      public:
        EntryDeleter(const EntryDeleter&)                   = delete;
        EntryDeleter&        operator=(const EntryDeleter&) = delete;
        static EntryDeleter& getInstance();  // 获取唯一实例
    };
}  // namespace FE::Sym

#endif  // __FRONTEND_SYMBOL_SYMBOL_ENTRY_H__
