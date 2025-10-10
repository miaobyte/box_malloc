#ifndef BOX_H
#define BOX_H

#include <blockmalloc/blockmalloc.h>
#include "obj_usage.h"

typedef struct
{
    #define BOX_MAGIC "boxmalloc"
    uint8_t magic[16]; // "boxmalloc"
    uint64_t boxhead_bytessize; // 伙伴系统的总size
    uint64_t box_bytessize;  // 总内存大小，不可变，内存长度必须=16^n*x,n>=1，x=[1,15]
    blocks_meta_t blocks;
} box_meta_t;

typedef enum
{
    BOX_UNUSED = 0,    // 未用（可以分配 obj、box）
    BOX_FORMATTED = 1, // box 已格式化
    OBJ_START = 2,     // obj_start
    OBJ_CONTINUED = 3  // obj_continued
} BoxState;

typedef struct
{
    uint8_t state : 2;       // 0=未用（可以分配obj、box）,1=已格式化为box，2=obj
    int8_t continue_max : 6; // 连续的最大空闲obj,[0~16]
} __attribute__((packed)) box_child_t;

typedef struct
{
    uint8_t state : 2;           // 0=未用（可以分配obj、box）,1=已格式化为box，2=obj
    int8_t max_obj_capacity : 6; // 连续的最大空闲obj,[0~16]

    // lock
    atomic_int_fast64_t rw_lock;  // 0=无锁，1=读锁，2=写锁（简化实现）

    // parent
    int32_t parent; // parent_blockid

    // box
    uint8_t objlevel; //[0,16] boxlevel=本层的objlevel+1

    // obj,childbox usage
    uint8_t avliable_slot;            // 【2，16】
    obj_usage child_max_obj_capacity; // 下层的最大对象容量
    box_child_t used_slots[16];

    // childbox
    int32_t childs_blockid[16];

} __attribute__((packed)) box_head_t; //

#endif // BOX_H