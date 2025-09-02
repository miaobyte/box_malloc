#include <stdint.h>
#include <stdlib.h>
#include "box_malloc.h"

void test_boxinit(void* metaptr){
    box_init(metaptr,1024*1024, 1024*1024*16);
}


int main(int argc, char *argv[]) {

    uint8_t *buddy = malloc(1024*1024);

    uint8_t *data = malloc(1024*1024*16);
    test_boxinit(buddy);

    void* p5=box_alloc(buddy,data,5);
    void* p7=box_alloc(buddy,data,7);

    box_free(buddy,data,p5);
    box_free(buddy,data,p7);

    free(buddy);
    free(data);
    return 0;
}