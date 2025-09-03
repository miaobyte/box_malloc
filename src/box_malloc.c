#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <block_malloc/block_malloc.h>

#include "box_malloc.h"
#include "obj_offset.h"
#include "logutil.h"

/*
内存布局示意图：

meta区：
+-------------------+  box_meta_t 描述整个伙伴系统
|   +-------------+ |
|   | buddysize    | |  伙伴系统的总大小
|   | box_size     | |  box区的总大小
|   | blocks_meta_t| |  blocks的元数据
|   +-------------+ |  blocks区，存储 block_t 和 box_head_t
|   | block_t0    | |  每个 block 的元数据
|   | box_head_t[0] | |  描述第0个 box 的状态和结构
|   +-------------+ |
|   | block_t1    | |  每个 block 的元数据
|   | box_head_t[1] | |  描述第1个 box 的状态和结构
|   +-------------+ |
|   | block_t2    | |  每个 block 的元数据
|   | box_head_t[2] | |  描述第2个 box 的状态和结构
|   | ...         | |
|   +-------------+ |
+-------------------+  <-- meta区结束，box区开始

box区：
+-------------------+  <-- 起始地址 (box_root)
|   box 数据区      |  存储实际分配的对象，不包含任何 meta 信息
|                   |
|                   |
+-------------------+  <-- box区结束

说明：
1. meta区和 box区 地址互相独立。
2. meta区存储 box_meta_t 和 blocks 的元数据，用于管理 box 的分配。
3. box区是实际分配的内存区域，不存储任何元数据。
*/


typedef struct
{
    uint64_t buddysize; // 伙伴系统的总size
    uint64_t box_size;  // 总内存大小，不可变，内存长度必须=16^n*x,n>=1，x=[1,15]
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

static void box_format(box_meta_t *meta, box_head_t *node, uint8_t objlevel, uint8_t avliable_slot, int32_t parent_id);

static void *block_start(void *meta){
    return meta + sizeof(box_meta_t);
}

int box_init(void *metaptr,const size_t buddysize,const size_t box_size)
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
    box_meta_t *meta = metaptr;
    *meta = (box_meta_t){
        .buddysize = buddysize,
        .box_size = box_size,
    };
 
    blocks_init(&meta->blocks,buddysize - sizeof(box_meta_t), sizeof(box_head_t));
    int64_t block_id = blocks_alloc(&meta->blocks,block_start(meta)); // 分配根节点
    if(block_id<0){
        LOG("Failed to allocate root block");
        return -1;
    }

    box_head_t *root_boxhead = block_start(meta) + block_data_offset(&meta->blocks, block_id);
    if (!root_boxhead)
    {
        LOG("Failed to allocate root node");
        return -1;
    }

    box_format(meta, root_boxhead, rounded_size_t.level, rounded_size_t.multiple, -1);
    LOG("box_init success");
    return 0;
}
static uint8_t box_continuous_max(box_head_t *node)
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
static void box_format(box_meta_t *meta, box_head_t *node, uint8_t objlevel, uint8_t avliable_slot, int32_t parent_id)
{
    node->state = BOX_FORMATTED;

    node->objlevel = objlevel;

    // obj,childbox usage
    node->avliable_slot = avliable_slot;
    node->max_obj_capacity = avliable_slot;
    for (int i = 0; i < avliable_slot; i++)
    {
        node->used_slots[i] = (box_child_t){
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
static obj_usage box_max_obj_capacity(box_head_t *node)
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

static void update_parent(box_meta_t *meta, box_head_t *node, bool slotstate_changed, bool slot_mac_obj_capacity_changed)
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

            box_head_t *child = NULL;
            for (int i = 0; i < node->avliable_slot; i++)
            {
                if (node->used_slots[i].state == BOX_FORMATTED)
                {
                    child = block_start(meta) + block_data_offset(&meta->blocks, node->childs_blockid[i]);
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
            box_head_t *parent = block_start(meta) + block_data_offset(&meta->blocks, node->parent);
            update_parent(meta, parent, slotstate_changed, slot_mac_obj_capacity_changed);
        }
    }
}

static uint8_t put_slots(box_meta_t *meta, box_head_t *node, obj_usage objsize)
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
        box_head_t *parent = block_start(meta) + block_data_offset(&meta->blocks, node->parent);
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
static uint64_t box_find_alloc(box_meta_t *meta, box_head_t *node, box_head_t *parent, obj_usage objsize)
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
            LOG("Allocated at level %d, slot %d+%d", node->objlevel, target_slot, objsize.multiple-1);
            return obj_offset((obj_usage){
                .level = node->objlevel,
                .multiple = target_slot,
            });
        }
        else if (objsize.level < node->objlevel)
        {
            // 目标体量<当前level，查找子节点
            box_head_t *child = NULL;
            for (int i = 0; i < node->avliable_slot; i++)
            {
                if (node->childs_blockid[i] < 0)
                {
                    // 需要分配出来
                    int64_t child_block_id = blocks_alloc(&(meta->blocks), block_start(meta));
                    if(child_block_id<0){
                        LOG("Failed to allocate root block");
                        return -1;
                    }
                    node->childs_blockid[i] = child_block_id;
                    child = block_start(meta) + block_data_offset(&meta->blocks, child_block_id);

                    int64_t cur_block_id =   block_id_bydataoffset(&meta->blocks, (void*)node - block_start(meta));
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
                    child = block_start(meta) + block_data_offset(&meta->blocks, node->childs_blockid[i]);
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

void *box_alloc(void *metaptr,void *box_start,const size_t size)
{
    obj_usage aligned_objsize = align_to((size + 8 - 1) / 8);

    box_meta_t *meta = metaptr;
    box_head_t *root = block_start(meta) + block_data_offset(&meta->blocks, 0);
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

    return box_start + offset;
}
void box_free(void *metaptr,void *box_start,const void *ptr)
{
    box_meta_t *meta = metaptr;

    // 计算偏移量
    uint64_t offset = ((uint8_t *)ptr - (uint8_t *)box_start) / 8;

    // 获取根节点
    box_head_t *node = block_start(meta) + block_data_offset(&meta->blocks, 0);
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
            node = block_start(meta) + block_data_offset(&meta->blocks, node->childs_blockid[slot_index]);
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
            box_head_t *parent = block_start(meta) + block_data_offset(&meta->blocks, node->parent);
            update_parent(meta, parent, false, true);
        }

        LOG("Object successfully freed");
    }else
    {
        LOG("Error: Object not found in the boxmalloc");
    }
}
