#ifndef BOX_MALLOC_H
#define BOX_MALLOC_H

#include <stddef.h>

int box_init(void *meta,size_t buddysize, void *boxstart, size_t box_size);
void *box_alloc(void *meta, size_t size);
void box_free(void *meta, void *ptr);

#endif // BOX_MALLOC_H