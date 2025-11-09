#include <frontend/symbol/symbol_entry.h>
using namespace std;
using namespace FE::Sym;

// 静态成员变量初始化
unordered_map<string, Entry*> Entry::entryMap;

/**
 * 清理所有Entry对象，释放内存
 */
void Entry::clear()
{
    // 遍历entryMap，删除所有Entry对象，释放内存
    for (auto& [name, entry] : entryMap)
    {
        if (!entry) continue;
        delete entry;
        entry = nullptr;
    }
    entryMap.clear();
}

/**
 * 创建或获取唯一的Entry对象
 * @note 如果entryMap中已存在该名字，则直接返回已有的Entry，否则创建新的Entry并存入entryMap
 */
Entry* Entry::getEntry(string name)
{
    // 找Entry对象，如果不存在则创建新的Entry并存入entryMap
    if (entryMap.find(name) == entryMap.end()) entryMap[name] = new Entry(name);
    return entryMap[name];
}

Entry::Entry(string name) : name(name) {}

// 获取符号名字
const string& Entry::getName() { return name; }

EntryDeleter::EntryDeleter() {}
EntryDeleter::~EntryDeleter() { Entry::clear(); }
EntryDeleter& EntryDeleter::getInstance()
{
    // 创建单例实例
    static EntryDeleter instance;
    return instance;
}
namespace
{
    // 确保程序结束时EntryDeleter的唯一实例被创建，从而调用析构函数清理Entry对象
    EntryDeleter& instance = EntryDeleter::getInstance();
}  // namespace
