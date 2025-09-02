#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
obj_usage以16为基数的幂和倍数
*/
typedef struct
{
    uint8_t level : 4;    // obj最大的level
    uint8_t multiple : 4; // obj最长连续可用的slots [1,15],如果==0,说明无可用
} __attribute__((packed)) obj_usage;

static uint64_t int_pow(uint64_t base, uint32_t exp)
{
    uint64_t result = 1;
    for (uint32_t i = 0; i < exp; i++)
    {
        result *= base;
    }
    return result;
}
static uint32_t int_log(uint64_t n, uint32_t base)
{
    uint32_t log = 0;
    while (n >= base)
    {
        n /= base;
        log++;
    }
    return log;
}
static obj_usage align_to(uint64_t n)
{
    uint32_t base = 16;
    obj_usage result = {0, 0};
    if (n < base)
    {
        result.multiple = n;
        return result;
    }

    result.level = int_log(n, base);
    uint64_t minbase = int_pow(base, result.level);

    result.multiple = (n + minbase - 1) / minbase;
    if (result.multiple >= base)
    {
        result.multiple = 1;
        result.level++;
        minbase = int_pow(base, result.level);
    }
    return result;
}
static int8_t compare_obj_usage(const obj_usage a, obj_usage b)
{
    if (a.level != b.level)
        return a.level - b.level;
    return a.multiple - b.multiple;
}
static uint64_t obj_offset(const obj_usage a)
{
    uint64_t offset = 8;
    for (int i = 0; i < a.level; i++)
    {
        offset *= 16;
    }
    offset *= (a.multiple);
    return offset;
}