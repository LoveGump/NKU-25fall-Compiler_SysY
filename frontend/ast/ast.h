#ifndef __FRONTEND_AST_AST_H__
#define __FRONTEND_AST_AST_H__

#include <frontend/ast/ast_defs.h>
#include <frontend/ast/ast_visitor.h>
#include <frontend/symbol/symbol_entry.h>
#include <vector>

/*
 * ============================================================================
 * 功能: 抽象语法树(Abstract Syntax Tree, AST)的基础节点定义
 *
 *
 * 示例: int a = b + 3;
 *   源代码 -> Token序列 -> AST树
 *
 * 设计模式:
 *   - 组合模式(Composite Pattern): 节点可以包含子节点，形成树结构
 *   - 访问者模式(Visitor Pattern): 通过accept()方法支持不同的遍历操作
 *
 * Lab2阶段:
 *   - 构建AST：通过语法分析将源代码转换为AST
 *   - 打印AST：使用Visitor遍历并输出树结构
 *
 * Lab3阶段:
 *   - 语义分析：使用Visitor检查类型、作用域等
 *   - 代码生成：使用Visitor将AST转换为中间代码(LLVM IR)
 * ============================================================================
 */

/*
如果需要，你可以在类中添加成员变量和成员函数，辅助你完成实验
*/
// AST的节点类
//
// 功能: 所有AST节点的公共基类
//       定义了所有节点共有的属性和接口
//
// 继承层次:
//   Node (基类)
//     ├── ExprNode (表达式节点)
//     │     ├── LiteralExpr (字面量: 42, 3.14)
//     │     ├── BinaryExpr (二元运算: a+b)
//     │     ├── UnaryExpr (一元运算: -a, !b)
//     │     ├── CallExpr (函数调用: func(args))
//     │     └── ...
//     ├── StmtNode (语句节点)
//     │     ├── ExprStmt (表达式语句: a=b;)
//     │     ├── IfStmt (if语句)
//     │     ├── ForStmt (for循环)
//     │     └── ...
//     └── DeclNode (声明节点)
//           ├── VarDeclarator (变量声明符)
//           ├── ParamDeclarator (参数声明符)
//           └── ...

// -------- 前向声明 --------
// 声明节点类型，避免循环依赖
// -------- 位置信息 --------

// line_num: 节点在源代码中的行号
// 用途:
//   1. 错误报告: "Error at line 42: ..."
//   2. 调试信息: 生成的代码对应的源代码行
//   3. 断点设置: 调试器需要知道代码位置
// -------- 虚析构函数 --------

// 为什么必须是虚函数？
//
// 问题场景:
//   Node* node = new BinaryExpr(...);  // 基类指针指向派生类对象
//   delete node;  // 如果析构函数不是虚函数会发生什么？
//
// 如果析构函数不是虚函数:
//   - 只调用Node的析构函数
//   - BinaryExpr的析构函数不会被调用
//   - 导致BinaryExpr中的资源(如lhs, rhs)没有被释放
//   - 造成内存泄漏
//
// 使用virtual后:
//   - 编译器生成虚函数表(vtable)
//   - delete会根据实际类型调用正确的析构函数
//   - 析构顺序: BinaryExpr -> ExprNode -> Node (从派生到基类)
//   - 保证所有资源都被正确释放
//
// = default: 使用编译器生成的默认实现
//   对于基类Node，没有需要特殊清理的资源
// AST的根节点
//
// 功能: 表示整个编译单元(源文件)的根节点
//       包含所有顶层的语句(全局变量、函数定义等)
//
// 为什么需要Root节点:
//   1. 统一入口: 所有的遍历操作从root开始
//   2. 明确边界: 清楚地标识AST的开始和结束
//   3. 方便管理: 统一管理所有顶层节点的生命周期
// stmts: 顶层语句列表
// 包含的内容:
//   - 全局变量声明: int a = 5;
//   - 函数定义: int main() { ... }
//   - (可选)全局常量: const float PI = 3.14;
//
// 为什么用指针?
//   vector<StmtNode*>*: 指向vector的指针
//   - vector在堆上分配，Root负责管理其生命周期
//   - 可以高效地传递和移动大型数据结构

namespace FE::AST
{
    class StmtNode;        // 语句节点基类
    class ExprNode;        // 表达式节点基类
    class DeclNode;        // 声明节点基类
    class VarDeclaration;  // 变量声明节点

    using Entry = FE::Sym::Entry;  // 符号表项

    // AST节点基类
    class Node
    {
      public:
        int      line_num;  // 行号
        int      col_num;   // 列号
        NodeAttr attr;      // 节点属性，用于存储操作符和表达式值等信息

        Node(int line_num = -1, int col_num = -1) : line_num(line_num), col_num(col_num), attr() {}
        virtual ~Node() = default;

        virtual void accept(Visitor& visitor) = 0;
    };

    class Root : public Node
    {
      private:
        std::vector<StmtNode*>* stmts;  // 顶层语句列表

      public:
        Root(std::vector<StmtNode*>* stmts) : Node(-1, -1), stmts(stmts) {}
        virtual ~Root() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }

        std::vector<StmtNode*>* getStmts() const { return stmts; }
    };
}  // namespace FE::AST

#endif  // __FRONTEND_AST_AST_H__
