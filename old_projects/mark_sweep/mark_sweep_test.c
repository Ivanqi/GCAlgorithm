#include "gc.h"

/**
 * 1. 测试条件保证申请内存保证在TINY_HEAP_SIZE下，因为大于这个数会发生gc
 * 测试 分配和 释放是否正常
 */

void test_malloc_free(size_t req_size){
    printf("-----------测试内存申请与释放------------\n");
    printf("-----------***************------------\n");
    //在回收p1的情况下 p2的申请将复用p1的地址
    void *p1 = gc_malloc(req_size);
    gc_free(p1);
    void *p2 = gc_malloc(req_size);
    //在上面 p1被释放了，p2 从新申请 会继续从堆的起始位置开始分配 所以 内存地址是一样的
    assert(p1 == p2);
    gc_free(p2);

    //在不清除p1的情况下 p2 会申请不同内存
    p1 = gc_malloc(req_size);
    p2 = gc_malloc(req_size);

    if (req_size == TINY_HEAP_SIZE) {
        assert(p1 == p2);
    } else {
        assert(p1 != p2);
    }

    printf("-----------   passing     ------------\n\n");
    // clear();
}

int main () {

    //小内存测试，
    test_malloc_free(8);
    return 0;
}