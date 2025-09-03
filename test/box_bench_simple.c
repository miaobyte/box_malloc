#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include "box_malloc.h"

#define NUM_ALLOCS 100

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
    size_t sizes[] = {4, 34, 2355, 673, 3348};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    clock_t start = clock();
    // 分配内存
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        size_t size = sizes[i % num_sizes];
        void *ptr = box_alloc(buddy, data, size);
        if (ptr == NULL)
        {
            printf("box_alloc failed at iteration %d\n", i);
            // 释放已分配的内存
            for (int j = 0; j < i; j++)
            {
                box_free(buddy, data, data + offsets[j]);
            }
            free(buddy);
            free(data);
            return 1;
        }
        offsets[i] = (uint8_t *)ptr - data;
    }

    // 释放内存
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        box_free(buddy, data, data + offsets[i]);
    }

    clock_t end = clock();

    printf("Time taken: %f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    // 释放元数据区和数据区
    free(buddy);
    free(data);
    return 0;
}