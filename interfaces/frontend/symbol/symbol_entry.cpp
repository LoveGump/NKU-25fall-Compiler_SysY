#include <frontend/symbol/symbol_entry.h>
using namespace std;
using namespace FE::Sym;

unordered_map<string, Entry*> Entry::entryMap;

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

Entry* Entry::getEntry(string name)
{
    // 找Entry对象，如果不存在则创建新的Entry并存入entryMap
    if (entryMap.find(name) == entryMap.end()) entryMap[name] = new Entry(name);
    return entryMap[name];
}

Entry::Entry(string name) : name(name) {}

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
    EntryDeleter& instance = EntryDeleter::getInstance();
}
