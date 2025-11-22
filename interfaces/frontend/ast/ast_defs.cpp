#include <frontend/ast/ast_defs.h>
#include <debug.h>
#include <sstream>

std::ostream& operator<<(std::ostream& os, FE::AST::Operator op)
{
    switch (op)
    {
#define X(name, lname, idx) \
    case FE::AST::Operator::name: return os << #lname;
        AST_OPERATOR_DECL
#undef X
        default: return os << "UNKNOWN OPERATOR";
    }
}

std::string toString(FE::AST::Operator op)
{
    std::stringstream ss;
    ss << op;
    return ss.str();
}

namespace FE::AST
{
    Type* voidType  = nullptr;
    Type* boolType  = nullptr;
    Type* intType   = nullptr;
    Type* llType    = nullptr;
    Type* floatType = nullptr;

    std::array<Type*, maxTypeIdx + 1>                        TypeFactory::baseTypes = {nullptr};
    std::map<TypeFactory::PtrBase_t, TypeFactory::PtrType_t> TypeFactory::ptrTypeMap;

    TypeFactory::TypeFactory()
    {
#define X(name, lname, idx) baseTypes[idx] = new BasicType(Type_t::name);
        AST_BASETYPE_DECL
#undef X

        voidType  = getBasicType(Type_t::VOID);
        boolType  = getBasicType(Type_t::BOOL);
        intType   = getBasicType(Type_t::INT);
        llType    = getBasicType(Type_t::LL);
        floatType = getBasicType(Type_t::FLOAT);
    }

    TypeFactory::~TypeFactory()
    {
        for (auto& [k, v] : ptrTypeMap)
        {
            if (!v) continue;
            delete v;
            v = nullptr;
        }
        ptrTypeMap.clear();
        for (auto& t : baseTypes)
        {
            if (!t) continue;
            delete t;
            t = nullptr;
        }
    }

    // 获取基础类型实例
    Type* TypeFactory::getBasicType(Type_t t) { return baseTypes[static_cast<size_t>(t)]; }
    // 获取指针类型实例
    Type* TypeFactory::getPtrType(Type* t)
    {
        if (!t) return nullptr;
        auto it = ptrTypeMap.find(t);
        if (it != ptrTypeMap.end()) return it->second;
        Type* ptype   = new PtrType(t);
        ptrTypeMap[t] = ptype;
        return ptype;
    }

    TypeFactory& tf = TypeFactory::getInstance();

    VarValue::Proxy::operator bool() const
    {
        ASSERT(obj->type == boolType);
        return obj->boolValue;
    }

    VarValue::Proxy::operator int() const
    {
        ASSERT(obj->type == intType);
        return obj->intValue;
    }

    VarValue::Proxy::operator long long() const
    {
        ASSERT(obj->type == llType);
        return obj->llValue;
    }

    VarValue::Proxy::operator float() const
    {
        ASSERT(obj->type == floatType);
        return obj->floatValue;
    }
}  // namespace FE::AST
