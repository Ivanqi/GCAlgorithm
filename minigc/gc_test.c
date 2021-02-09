#include <stdio.h>
#include <assert.h>
#include "gc.h"



static void test_mini_gc_malloc_free() {
    void *p1, *p2, *p3;

    // malloc check
    p1 = (void *)mini_gc_malloc(10);
    p2 = (void *)mini_gc_malloc(10);
    p3 = (void *)mini_gc_malloc(10);

    assert(((Header*)p1 - 1)->size == ALIGN(10, PTRSIZE));
    assert(((Header *)p1 - 1)->flags == FL_ALLOC);
    // assert((Header *)(((size_t)(getFreeList() + 1)) + getFreeList()->size) == ((Header*)p3 - 1));
}

int main() {
    gc_init();
    test_mini_gc_malloc_free();
    return 0;
}