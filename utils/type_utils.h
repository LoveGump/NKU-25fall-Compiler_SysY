#ifndef __UTILS_TYPE_UTILS_H__
#define __UTILS_TYPE_UTILS_H__

#include <cstddef>

template <typename T>
size_t getTID()
{
    // 返回当前函数的地址作为类型 T 的唯一 ID
    return (size_t)(&getTID<T>);
}

#endif  // __UTILS_TYPE_UTILS_H__
