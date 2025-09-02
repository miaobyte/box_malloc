#ifndef BOX_MALLOC_H
#define BOX_MALLOC_H

#include <stddef.h>

int box_init(void *metaptr,const size_t buddysize,const size_t box_size);
void *box_alloc(void *metaptr,void *box_start,const size_t size);
void box_free(void *metaptr,void *box_start,const void *ptr);

#endif // BOX_MALLOC_H