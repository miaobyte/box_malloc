#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <block_malloc/block_malloc.h>

#include "box_malloc.h"
#include "obj_offset.h"
#include "logutil.h"

static void rlock(_Atomic int64_t *lock) {
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) == 2) {
        // 自旋等待写锁释放
    }
}

static void runlock(_Atomic int64_t *lock) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

static void lock(_Atomic int64_t *lock) {
    while (__atomic_exchange_n(lock, 2, __ATOMIC_ACQUIRE) != 0) {
        // 自旋等待锁释放
    }
}

static void unlock(_Atomic int64_t *lock) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

typedef struct
{
    #define BOX_MAGIC "box_malloc"
    uint8_t magic[10]; // "box_malloc"
    uint64_t boxhead_bytessize; // 伙伴系统的总size
    uint64_t box_bytessize;  // 总内存大小，不可变，内存长度必须=16^n*x,n>=1，x=[1,15]
    blocks_meta_t blocks;
} box_meta_t;

static int check_magic(box_meta_t *meta) {
    if (memcmp(meta->magic, BOX_MAGIC, sizeof(meta->magic)) != 0) {
        return -1;
    }
    return 0;
}
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
    _Atomic int64_t rw_lock;  // 0=无锁，1=读锁，2=写锁（简化实现）

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

int box_init(void *metaptr, const size_t boxhead_bytessize, const size_t box_bytessize)
{
    if(check_magic((box_meta_t *)metaptr) == 0) {
        LOG("[ERROR] box_meta_t already initialized");
        return -1;
    }

    if (box_bytessize % 8 != 0)
    {
        LOG("[ERROR] box_bytessize must be aligned to 8. Given size: %zu", box_bytessize);
        return -1;
    }
    obj_usage rounded_size_t = align_to(box_bytessize / 8);
    if (box_bytessize != obj_offset(rounded_size_t))
    {
        LOG("[ERROR] box_bytessize must be aligned to 16. Given size: %zu", box_bytessize);
        return -1;
    }
    box_meta_t *meta = metaptr;
    *meta = (box_meta_t){
        .boxhead_bytessize = boxhead_bytessize,
        .box_bytessize = box_bytessize,
    };

    blocks_init(&meta->blocks, boxhead_bytessize - sizeof(box_meta_t), sizeof(box_head_t));

    void *boxhead=meta+sizeof(box_meta_t);
    int64_t block_id = blocks_alloc(&meta->blocks, boxhead); // 分配根节点
    if (block_id < 0)
    {
        LOG("[ERROR] failed to allocate root block");
        return -1;
    }

    box_head_t *root_boxhead = boxhead + blockdata_offset(&meta->blocks, block_id);
    box_format(meta, root_boxhead, rounded_size_t.level, rounded_size_t.multiple, -1);
    
    memcpy(meta->magic, BOX_MAGIC, sizeof(meta->magic));
    LOG("[INFO] box_init success");
    return 0;
}
/*
 * 线程安全需求：
 * - 需要读锁：只读取槽位状态，不修改。
 * - 锁粒度：node 级，获取当前节点的读锁。
 * - 锁顺序：单个节点。
 * - 并发性：允许多个线程同时计算同一节点。
 */
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
/*
 * 线程安全需求：
 * - 需要写锁：初始化节点状态。
 * - 锁粒度：node 级，获取当前节点的写锁。
 * - 锁顺序：单个节点。
 * - 并发性：不同节点的格式化可以并发。
*/
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
/*
 * 线程安全需求：
 * - 需要读锁：只读取节点容量信息，不修改。
 * - 锁粒度：node 级，获取当前节点的读锁。
 * - 锁顺序：单个节点。
 * - 并发性：允许多个线程同时读取。
 */
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
/*
 * 线程安全需求：
 * - 需要写锁：修改父节点状态。
 * - 锁粒度：node 级，递归获取当前节点的写锁。
 * - 锁顺序：从叶到根逐级获取锁。
 * - 并发性：不同分支的更新可以并发。
 */
static void update_parent(box_meta_t *meta, box_head_t *node, bool slotstate_changed, bool slot_max_obj_capacity_changed)
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

    void *boxhead=meta+sizeof(box_meta_t);

    if (slot_max_obj_capacity_changed)
    {
        if (node->max_obj_capacity > 0)
        {
            // 当前节点还有空闲槽，child_max_obj_capacity不变
            slot_max_obj_capacity_changed = false;
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
                    child = boxhead + blockdata_offset(&meta->blocks, node->childs_blockid[i]);
                    if (!child)
                    {
                        LOG("[ERROR] child node should not be NULL");
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
                slot_max_obj_capacity_changed = false;
            }
        }
    }
    if (slotstate_changed || slot_max_obj_capacity_changed)
    {
        if (node->parent >= 0)
        {
            box_head_t *parent = boxhead + blockdata_offset(&meta->blocks, node->parent);
            update_parent(meta, parent, slotstate_changed, slot_max_obj_capacity_changed);
        }
    }
}
/*
 * 线程安全需求：
 * - 需要写锁：修改节点的槽位状态。
 * - 锁粒度：node 级，获取当前节点的写锁。
 * - 锁顺序：单个节点，无递归。
 * - 并发性：不同节点的 put_slots 可以并发。
 */
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
        LOG("[ERROR] not enough continuous free slots");
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

    void *boxhead=meta+sizeof(box_meta_t);
    if (node->max_obj_capacity != continuous_max)
    {
        // 发生变化，递归更新parent的child
        node->max_obj_capacity = continuous_max;
        box_head_t *parent = boxhead + blockdata_offset(&meta->blocks, node->parent);
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

 * 线程安全需求：
 * - 需要写锁：修改节点状态（分配子节点或槽位）。
 * - 锁粒度：node 级，递归获取当前节点的写锁。
 * - 锁顺序：从根到叶逐级获取锁。
 * - 并发性：不同分支可以并发查找/分配。
 */
static uint64_t box_find_alloc(box_meta_t *meta, box_head_t *node, box_head_t *parent, obj_usage objsize)
{
    if (!node)
    {
        LOG("[ERROR] node is NULL");
        return BOX_FAILED; // 表示分配失败
    }
    void *boxhead=meta+sizeof(box_meta_t);
    if (node->state == BOX_FORMATTED)
    {
        if (objsize.level == node->objlevel)
        {
            // 目标体量属于当前level，且剩余slots满足obj，直接在当前node的slots中分配

            uint8_t target_slot = put_slots(meta, node, objsize);
            uint64_t offset= obj_offset((obj_usage){
                .level = node->objlevel,
                .multiple = target_slot,
            });
            LOG("[INFO] allocated at level %d, slot [%d,%d],size %lu",node->objlevel, target_slot, target_slot+objsize.multiple - 1, obj_offset(objsize));
            
            return offset;
        }
        else if (objsize.level < node->objlevel)
        {
            // 目标体量<当前level，继续查找子节点
            // TODO，slots
            box_head_t *child = NULL;
            for (int i = 0; i < node->avliable_slot; i++)
            {
                if (node->childs_blockid[i] >= 0)
                {
                    child = boxhead + blockdata_offset(&meta->blocks, node->childs_blockid[i]);
                    obj_usage child_max = box_max_obj_capacity(child);
                    if (compare_obj_usage(child_max, objsize) >= 0)
                    {
                        uint64_t offset = obj_offset((obj_usage){
                            .level = node->objlevel,
                            .multiple = i,
                        });
                        uint64_t target_box= box_find_alloc(meta, child, node, objsize);
                        if (target_box == BOX_FAILED)
                        {
                            LOG("[ERROR] box_find_alloc failed");
                            return BOX_FAILED;
                        }
                        return offset + target_box;
                    }
                }
                else if (node->used_slots[i].state == BOX_UNUSED) // 添加检查：确保slot空闲
                {
                    // 需要新建child box_head_t
                    int64_t child_block_id = blocks_alloc(&(meta->blocks), boxhead);
                    if (child_block_id < 0)
                    {
                        LOG("[ERROR] failed to create box_head for child");
                        return BOX_FAILED;
                    }
                    node->childs_blockid[i] = child_block_id;

                    child = boxhead+ blockdata_offset(&meta->blocks, node->childs_blockid[i]);

                    int64_t cur_block_id = blockid_bydataoffset(&meta->blocks, (void *)node - boxhead);
                    box_format(meta, child, node->objlevel - 1, 16, cur_block_id);

                    // 更新node中的child信息
                    node->used_slots[i].state = BOX_FORMATTED;
                    // 更新node中的max_obj_capacity
                    uint8_t new_max = box_continuous_max(node);
                    if (node->max_obj_capacity != new_max)
                    {
                        node->max_obj_capacity = new_max;
                    }

                    // 判断新child box的容量

                    obj_usage child_max = (obj_usage){
                        .level = child->objlevel + 1,
                        .multiple = 1,
                    };
                    if (compare_obj_usage(child_max, objsize) >= 0)
                    {
                        uint64_t offset = obj_offset((obj_usage){
                            .level = node->objlevel,
                            .multiple = i,
                        });
                        uint64_t target_box= box_find_alloc(meta, child, node, objsize);
                        if (target_box == BOX_FAILED)
                        {
                            LOG("[ERROR]:box_find_alloc failed");
                            return BOX_FAILED;
                        }
                        return offset + target_box;
                    }
                }
            }

            LOG("[ERROR] bug happen,but should not happen");
            return BOX_FAILED;
        }
    }

    // 如果执行到这里,需要联系开发者
    LOG("[ERROR] bug happen,but should not happen");
    return BOX_FAILED;
}

uint64_t box_alloc(void *metaptr, const size_t size)
{
    if (!metaptr)
    {
        LOG("[ERROR] root must not NULL");
        return BOX_FAILED;
    };

    obj_usage aligned_objsize = align_to((size + 8 - 1) / 8);

    box_meta_t *meta = metaptr;
    void *boxhead=meta+sizeof(box_meta_t);
    box_head_t *root = boxhead+ blockdata_offset(&meta->blocks, 0);
    
    obj_usage max_capacity = box_max_obj_capacity(root);

    if (compare_obj_usage(aligned_objsize, max_capacity) > 0)
    {
        LOG("[ERROR] requested size %zu is too large for the box", size);
        return BOX_FAILED;
    }
    uint64_t offset = box_find_alloc(meta, root, NULL, aligned_objsize);
    if (offset == BOX_FAILED)
        return BOX_FAILED;
    LOG("[INFO] object allocated at offset %lu", offset);
    return  offset;
}
/*
 * 线程安全需求：
 * - 需要读锁：只读取节点状态，不修改。
 * - 锁粒度：node 级，递归获取从根到目标节点的读锁。
 * - 锁顺序：从根到叶逐级获取锁。
 * - 并发性：允许多个线程同时查找同一分支。
 */
static box_head_t *find_obj_node(box_meta_t *meta, const uint64_t obj_offset, uint8_t *out_slot_index)
{
    // 转换为8字节单位的偏移量
    uint64_t unit_offset = obj_offset / 8;

    // 获取根节点
    void *boxhead=meta+sizeof(box_meta_t);
    box_head_t *node = boxhead + blockdata_offset(&meta->blocks, 0);
    if (!node)
    {
        LOG("[ERROR] root node is NULL");
        return NULL;
    }

    // 计算根节点的level
    uint8_t current_level = node->objlevel;

    // 从高位向低位逐层查找
    while (node && node->state == BOX_FORMATTED)
    {
        // 计算当前层级的槽位索引
        uint64_t divisor = 1;
        for (int i = 0; i < current_level; i++)
        {
            divisor *= 16;
        }

        uint8_t slot_index = (unit_offset / divisor) % 16;

        // 检查该槽位的状态
        if (node->used_slots[slot_index].state == OBJ_START)
        {
            // 找到了对象的起始位置
            *out_slot_index = slot_index;
            return node;
        }
        else if (node->used_slots[slot_index].state == BOX_FORMATTED)
        {
            // 进入子节点继续查找
            node = boxhead + blockdata_offset(&meta->blocks, node->childs_blockid[slot_index]);
            current_level--;
        }
        else
        {
            // 该位置不是对象起始位置也不是子节点
            LOG("[ERROR] bug happen,invalid state %d at slot %d, level %d",
                node->used_slots[slot_index].state, slot_index, current_level);
            return NULL;
        }
    }

    // 如果遍历完所有层级仍未找到对象
    LOG("[ERROR] object+%lu not found", obj_offset);
    return NULL;
}
void box_free(void *metaptr, const uint64_t obj_offset)
{
    box_meta_t *meta = metaptr;
    uint8_t slot_index = 0;

    // 查找对象所在的节点和槽位

    box_head_t *node = find_obj_node(meta, obj_offset, &slot_index);

    if (!node)
    {
        LOG("[ERROR] free failed: object+%lu not found", obj_offset);
        return;
    }

    // 释放槽位
    node->used_slots[slot_index].state = BOX_UNUSED;
    node->used_slots[slot_index].continue_max = 16;

    // 释放连续的OBJ_CONTINUED槽位
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
    uint8_t new_max = box_continuous_max(node);
    void *boxhead=meta+sizeof(box_meta_t);
    if (node->max_obj_capacity != new_max)
    {
        node->max_obj_capacity = new_max;
        if (node->parent >= 0)
        {
            box_head_t *parent =boxhead + blockdata_offset(&meta->blocks, node->parent);
            update_parent(meta, parent, false, true);
        }
    }

    LOG("[INFO] object+%lu freed", obj_offset);
}