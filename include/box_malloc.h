/*
box_malloc 是一个基于伙伴系统（buddy system）的存储分配器，用于高效管理任意size的obj。
它接收两块独立的完整内存：meta区（存储管理box系统的元数据）和obj区（存放实际obj数据），初始化后不再支持扩展2个区域的大小。

box可以看作16叉树，obj可以占据连续的几个树子节点，一旦被obj占据，则不能再分割子树。

box_malloc的设计灵感来自于现实世界的包装箱系统
大包装箱（外箱）可嵌套小包装箱（内箱），形成多层结构。
物品需要按其体积大小选择合适的包装箱进行存放。

box_malloc的设计目标，不仅仅是作为程序的内存分配器，还希望能成为oskernel、block设备的存储分配器，提供高效的obj分配和释放功能。
同时，box_malloc是一个被动的obj分配器，需要被外部调用，不会主动整理和移动对象。

关于meta区：
meta区依赖block_malloc(https://github.com/miaobyte/block_malloc)。
meta区的大小=sizeof(box_meta_t)+boxcount*sizeof(box_head_t)
meta区的大小约束了box的node数量，进而约束了obj的数量，需要根据实际需求进行合理配置

关于obj区：
obj区不会存放任何box系统的元数据（如对象地址、对象数据长度，这些会在meta区找到），完全分配给obj使用，但是obj实际分配会对齐到alloced_size=X*(16^N)*8字节,X∈[1,15],N>=0
obj区通常可以达到非常高的利用率，在任何分配状态下，如果再malloc足够多的小obj，利用率可以达到100%。
但相应的，如果小obj过多，会导致meta区也很大。

关于obj分配：
最小分配单元为8字节，按16的幂次方进行对齐，支持动态分配和释放对象
box_malloc并不对obj的size进行优化适配，各种size的obj均可分配。
16叉树的设计，相比其它伙伴系统的2叉

16叉深度更低，但是在每个深度，可能都需要遍历1-16个子节点。
二叉深度更深，但是每个深度只需要遍历1-2个子节点。
举例：
以16*16byte malloc 8byte为例子
16叉深度为1，时间复杂度为O(1~16)
而二叉深度为4，时间复杂度固定为O(4~8)

以(16^8)*8=32G为例子
box深度为8，时间复杂度为O(8*(1~16）)=O(8~128)
二叉树深度为32，时间复杂度为O(32*(1~2))=O(32~64)

关于obj释放：
释放obj时，box_malloc会检查所在node slots的状态，发现node的slots全部空闲，则释放该node，并递归检查和释放其parent node，直到root node
*/


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

#ifndef BOX_MALLOC_H
#define BOX_MALLOC_H

#include <stddef.h>

int box_init(void *metaptr,const size_t buddysize,const size_t box_size);
void *box_alloc(void *metaptr,void *box_start,const size_t size);
void box_free(void *metaptr,void *box_start,const void *ptr);

#endif // BOX_MALLOC_H