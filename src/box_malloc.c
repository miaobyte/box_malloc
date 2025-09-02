#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <block_malloc/block_malloc.h>

#include "box_malloc.h"
#include "logutil.h"

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

typedef struct
{
    uint64_t buddysize; // 伙伴系统的总size
    void *rootbox;      // 指向块内存起始位置
    uint64_t box_size;  // 总内存大小，不可变，内存长度必须=16^n*x,n>=1，x=[1,15]

    blocks_meta_t blocks;
} box_meta;

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
} __attribute__((packed)) box_child;

typedef struct
{
    uint8_t state : 2;           // 0=未用（可以分配obj、box）,1=已格式化为box，2=obj
    int8_t max_obj_capacity : 6; // 连续的最大空闲obj,[0~16]
    // parent
    int32_t parent; // parent_blockid

    // box
    uint8_t objlevel; //[0,16] boxlevel=本层的objlevel+1

    // obj,childbox usage
    uint8_t avliable_slot;            // 【2，16】
    obj_usage child_max_obj_capacity; // 下层的最大对象容量
    box_child used_slots[16];

    // childbox
    int32_t childs_blockid[16];

} __attribute__((packed)) box_head; //

static void box_format(box_meta *meta, box_head *node, uint8_t objlevel, uint8_t avliable_slot, int32_t parent_id);

int box_init(void *metaptr, size_t buddysize, void *boxstart, size_t box_size)
{
    if (box_size % 8 != 0)
    {
        LOG("box_size must be aligned to 8. Given size: %zu", box_size);
        return -1;
    }
    obj_usage rounded_size_t = align_to(box_size / 8);
    if (box_size != obj_offset(rounded_size_t))
    {
        LOG("box_size must be aligned to 16. Given size: %zu", box_size);
        return -1;
    }
    box_meta *meta = metaptr;
    *meta = (box_meta){
        .buddysize = buddysize,
        .rootbox = boxstart,
        .box_size = box_size,
    };
    void *block_start = metaptr + sizeof(box_meta);
    blocks_init(block_start,buddysize - sizeof(box_meta), sizeof(box_head), &meta->blocks);
    int64_t block_id = blocks_alloc(&meta->blocks); // 分配根节点
    if(block_id<0){
        LOG("Failed to allocate root block");
        return -1;
    }
    box_head *root_boxhead = block_data(&meta->blocks, block_id);
    if (!root_boxhead)
    {
        LOG("Failed to allocate root node");
        return -1;
    }

    box_format(meta, root_boxhead, rounded_size_t.level, rounded_size_t.multiple, -1);
    LOG("box_init success");
    return 0;
}
static uint8_t box_continuous_max(box_head *node)
{
    uint8_t continuous_count = 0;
    uint8_t continuous_max = 0;
    for (int i = 0; i < node->avliable_slot; i++)
    {
        if (node->used_slots[i].state == BOX_UNUSED)
        {
            continuous_count++;
        }
        else
        {
            if (continuous_count > continuous_max)
                continuous_max = continuous_count;
            continuous_count = 0; // 中断，重新计数
        }
    }
    if (continuous_count > continuous_max)
        continuous_max = continuous_count;
    return continuous_max;
}
static void box_format(box_meta *meta, box_head *node, uint8_t objlevel, uint8_t avliable_slot, int32_t parent_id)
{
    node->state = BOX_FORMATTED;

    node->objlevel = objlevel;

    // obj,childbox usage
    node->avliable_slot = avliable_slot;
    node->max_obj_capacity = avliable_slot;
    for (int i = 0; i < avliable_slot; i++)
    {
        node->used_slots[i] = (box_child){
            .continue_max = 16,
            .state = BOX_UNUSED,
        };
    }

    // childbox
    for (int i = 0; i < 16; i++)
    {
        node->childs_blockid[i] = -1;
    }

    // parent
    node->parent = parent_id;
   
}
static obj_usage box_max_obj_capacity(box_head *node)
{
    if (node->max_obj_capacity > 0)
    {
        if (node->max_obj_capacity == 16)
        {
            return (obj_usage){
                .level = node->objlevel + 1,
                .multiple = 1,
            };
        }
        return (obj_usage){
            .level = node->objlevel,
            .multiple = node->max_obj_capacity,
        };
    }
    else
    {
        return node->child_max_obj_capacity;
    }
}

static void update_parent(box_meta *meta, box_head *node, bool slotstate_changed, bool slot_mac_obj_capacity_changed)
{
    if (slotstate_changed)
    {
        uint8_t newcontinuous_max = box_continuous_max(node);
        if (node->max_obj_capacity != newcontinuous_max)
        {
            node->max_obj_capacity = newcontinuous_max;
        }
        else
        {
            slotstate_changed = false;
        }
    }
    if (slot_mac_obj_capacity_changed)
    {
        if (node->max_obj_capacity > 0)
        {
            // 当前节点还有空闲槽，child_max_obj_capacity不变
            slot_mac_obj_capacity_changed = false;
        }
        else
        {
            obj_usage newmax = {
                .level = 0,
                .multiple = 0};

            box_head *child = NULL;
            for (int i = 0; i < node->avliable_slot; i++)
            {
                if (node->used_slots[i].state == BOX_FORMATTED)
                {
                    child = block_data(&meta->blocks, node->childs_blockid[i]);
                    if (!child)
                    {
                        LOG("Error: Child node should not be NULL");
                        return;
                    }
                    obj_usage childmax = box_max_obj_capacity(child);
                    if (compare_obj_usage(childmax, newmax) > 0)
                        newmax = childmax;
                }
            }
            int8_t child_max_obj_capacity_changed = compare_obj_usage(newmax, node->child_max_obj_capacity);
            if (child_max_obj_capacity_changed != 0)
            {
                // 发生变化
                node->child_max_obj_capacity = newmax;
            }
            else
            {
                slot_mac_obj_capacity_changed = false;
            }
        }
    }
    if (slotstate_changed || slot_mac_obj_capacity_changed)
    {
        if (node->parent >= 0)
        {
            box_head *parent = block_data(&meta->blocks, node->parent);
            update_parent(meta, parent, slotstate_changed, slot_mac_obj_capacity_changed);
        }
    }
}

static uint8_t put_slots(box_meta *meta, box_head *node, obj_usage objsize)
{
    uint8_t target_slot = 0;
    uint8_t continuous_count = 0;
    bool found = false;

    // 寻找连续的空闲槽

    for (int i = 0; i < node->avliable_slot && !found; i++)
    {
        if (node->used_slots[i].state == BOX_UNUSED)
        {
            if (continuous_count == 0)
            {
                target_slot = i; // 记录连续空闲槽的起始位置
            }
            continuous_count++;

            if (continuous_count >= objsize.multiple)
            {
                found = true;
                break;
            }
        }
        else
        {
            continuous_count = 0; // 中断，重新计数
        }
    }

    if (!found)
    {
        // 通常不会执行到这里，因为调用此函数前，已经确保有足够的连续空闲槽
        LOG("Error: Not enough continuous free slots");
        return 0;
    }

    // 标记已分配的槽
    for (int i = 0; i < objsize.multiple; i++)
    {
        if (i == 0)
        {
            node->used_slots[target_slot + i].state = OBJ_START;
        }
        else
        {
            node->used_slots[target_slot + i].state = OBJ_CONTINUED;
        }
        node->used_slots[target_slot + i].continue_max = 0;
    }

    uint8_t continuous_max = box_continuous_max(node);

    if (node->max_obj_capacity != continuous_max)
    {
        // 发生变化，递归更新parent的child
        node->max_obj_capacity = continuous_max;
        box_head *parent = block_data(&meta->blocks, node->parent);
        update_parent(meta, parent, false, true);
    }
    return target_slot;
}

#define BOX_FAILED (uint64_t)-1

/*
box内存分配模型，最小单元为8byte，按16为比例分割和分配内存
其有2块区域
meta区，存放box_meta和box_head数组
data区，存放实际的box数据，完全分配给obj（需要向上对齐），不会存放任何结构体的meta信息
*/
static uint64_t box_find_alloc(box_meta *meta, box_head *node, box_head *parent, obj_usage objsize)
{
    if (!node)
    {
        LOG("Error: Node is NULL");
        return BOX_FAILED; // 表示分配失败
    }
    if (node->state == BOX_FORMATTED)
    {
        if (objsize.level == node->objlevel)
        {
            // 目标体量属于当前level

            uint8_t target_slot = put_slots(meta, node, objsize);
            LOG("Allocated at level %d, slot %d * %d", node->objlevel, target_slot, objsize.multiple);
            return obj_offset((obj_usage){
                .level = node->objlevel,
                .multiple = target_slot,
            });
        }
        else if (objsize.level < node->objlevel)
        {
            // 目标体量<当前level，查找子节点
            box_head *child = NULL;
            for (int i = 0; i < node->avliable_slot; i++)
            {
                if (node->childs_blockid[i] < 0)
                {
                    // 需要分配出来
                    int64_t child_block_id = blocks_alloc(&(meta->blocks));
                    if(child_block_id<0){
                        LOG("Failed to allocate root block");
                        return -1;
                    }
                    node->childs_blockid[i] = child_block_id;
                    child = block_data(&meta->blocks, child_block_id);
                    
                    int64_t cur_block_id = block_bydata(node);
                    box_format(meta, child, node->objlevel - 1, 16, cur_block_id);

                    // 更新node中的child信息
                    node->used_slots[i].state = BOX_FORMATTED;
                    // 更新node中的max_obj_capacity
                    uint8_t new_max = box_continuous_max(node);
                    if (node->max_obj_capacity != new_max)
                    {
                        node->max_obj_capacity = new_max;
                    }
                }
                else
                {
                    child = block_data(&meta->blocks, node->childs_blockid[i]);
                }

                obj_usage child_max = box_max_obj_capacity(child);
                if (compare_obj_usage(child_max, objsize) >= 0)
                {
                    uint64_t offset = obj_offset((obj_usage){
                        .level = node->objlevel,
                        .multiple = i,
                    });
                    return offset + box_find_alloc(meta, child, node, objsize);
                }
            }

            LOG("Error:Bug happen");
            return BOX_FAILED;
        }
    }

    // 如果执行到这里，说明没有找到合适的空间
    LOG("Error:Bug happen");
    return BOX_FAILED;
}

void *box_alloc(void *metaptr, size_t size)
{
    obj_usage aligned_objsize = align_to((size + 8 - 1) / 8);

    box_meta *meta = metaptr;
    box_head *root = block_data(&meta->blocks, 0);
    if (!root)
    {
        LOG("Error: Root is NULL");
        return NULL;
    };

    obj_usage max_capacity = box_max_obj_capacity(root);

    if (compare_obj_usage(aligned_objsize, max_capacity) > 0)
    {
        LOG("Error: Requested size %zu is too large for the box system", size);
        return NULL;
    }
    uint64_t offset = box_find_alloc(meta, root, NULL, aligned_objsize);
    if (offset == BOX_FAILED)
        return NULL;

    return meta->rootbox + offset;
}
void box_free(void *metaptr, void *ptr)
{
    box_meta *meta = metaptr;

    // 计算偏移量
    uint64_t offset = ((uint8_t *)ptr - (uint8_t *)meta->rootbox) / 8;

    // 获取根节点
    box_head *node = block_data(&meta->blocks, 0);
    if (!node)
    {
        LOG("Error: Root node is NULL");
        return;
    }

    // 遍历找到目标节点
    bool found = false;
    uint8_t slot_index = 0;
    while (!found)
    {
        slot_index = offset % 16; // 当前节点的槽位索引
        offset /= 16;             // 计算父节点的偏移量

        if (node->used_slots[slot_index].state == OBJ_START)
        {
            found = true;
            break;
        }
        else if (node->used_slots[slot_index].state == BOX_FORMATTED)
        {
            // 如果槽位是子节点，继续向下查找
            node = block_data(&meta->blocks, node->childs_blockid[slot_index]);
            if (!node)
            {
                LOG("Error: Child node is NULL");
                return;
            }
        }
        else
        {
            LOG("Error in free: Invalid state %d", node->used_slots[slot_index].state);
            return;
        }
    }
    if (found)
    {
        // 释放槽位
        node->used_slots[slot_index].state = BOX_UNUSED;
        node->used_slots[slot_index].continue_max = 16;
        for (int i = slot_index + 1; i < node->avliable_slot; i++)
        {
            if (node->used_slots[i].state == OBJ_CONTINUED)
            {
                node->used_slots[i].state = BOX_UNUSED;
                node->used_slots[i].continue_max = 16;
            }
            else
            {
                break;
            }
        }

        // 更新连续最大空闲槽位计数

        // todo:
        uint8_t new_max = box_continuous_max(node);
        if (node->max_obj_capacity != new_max)
        {
            node->max_obj_capacity = new_max;
            box_head *parent = block_data(&meta->blocks, node->parent);
            update_parent(meta, parent, false, true);
        }

        LOG("Object successfully freed");
    }else
    {
        LOG("Error: Object not found in the boxmalloc");
    }
}
