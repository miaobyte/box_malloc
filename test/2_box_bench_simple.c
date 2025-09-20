#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include <box_malloc/box_malloc.h>

#define NUM_ALLOCS 111

int main()
{

    // 初始化 box_malloc 的元数据和数据区
    uint8_t *buddy = malloc(1024 * 1024);     // 元数据区
    uint8_t *data = malloc(1024 * 1024 * 16); // 数据区
    // 初始化 box_malloc
    if (box_init(buddy, 1024 * 1024, 1024 * 1024 * 16) != 0)
    {
        printf("Failed to initialize box_malloc\n");
        return 1;
    }

    uint64_t offsets[NUM_ALLOCS];
    size_t sizes[] = {4, 34,346, 2355, 673, 3348};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    clock_t start = clock();
    
    // 分配内存
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        size_t size = sizes[i % num_sizes];
        uint64_t obj_offset = box_alloc(buddy,  size);
        if (obj_offset == UINT64_MAX-1)
        {
            printf("box_alloc failed at iteration %d\n", i);
            return 1;
        }
        *(uint64_t *)(data + obj_offset) = i;
        offsets[i] = obj_offset;
    }

    // 释放内存
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
 
        uint64_t actual = *(uint64_t *)(data + offsets[i]);
        printf("stored value = %lu\n",actual);
        box_free(buddy, offsets[i]);
    }

    clock_t end = clock();

    printf("Time taken: %f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    // 释放元数据区和数据区
    free(buddy);
    free(data);
    return 0;
}