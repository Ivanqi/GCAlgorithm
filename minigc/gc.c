#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <setjmp.h>
#include <assert.h>
#include "gc.h"

static Header* add_heap(size_t req_size) {
    void *p;
    Header *align_p;

    if (gc_heaps_used >= HEAP_LIMIT) {
        fputs("OutOfMemory Error", stderr);
        abort();
    }

    if (req_size < TINY_HEAP_SIZE) {
        req_size = TINY_HEAP_SIZE;
    }

    // 分配内存
    if ((p = sbrk(req_size + PTRSIZE + HEADER_SIZE)) == (void*)-1) {
        return NULL;
    }

    // address alignment. 地址对齐
    align_p = gc_heaps[gc_heaps_used].slot = (Header*)ALIGN((size_t)p, PTRSIZE);
    req_size = gc_heaps[gc_heaps_used].size = req_size;
    align_p->size = req_size;
    align_p->next_free = align_p;
    gc_heaps_used++;

    return align_p;
}

static Header* grow(size_t req_size) {
    Header *cp, *up;
    // 增加新内存
    if (!(cp = add_heap(req_size))) {
        return NULL;
    }

    up = (Header *) cp;
    mini_gc_free((void *) (up + 1));
    return free_list;
}

void* mini_gc_malloc(size_t req_size) {
    Header *p, *prevp;
    size_t do_gc = 0;

    req_size = ALIGN(req_size, PTRSIZE);
    if (req_size <= 0) {
        return NULL;
    }

    if ((prevp = free_list) == NULL) {
        if (!(p = add_heap(TINY_HEAP_SIZE))) {
            return NULL;
        }
        prevp = free_list = p;
    }

    for (p = prevp->next_free; ; prevp = p, p = p->next_free) {
        if (p->size >= req_size) {
            if (p->size == req_size) {  // 需分配的内存数量和当前内存数量相等时，不分配内存
                // just fit
                prevp->next_free = p->next_free;    // 切换下一个next_free,为了寻找大于req_size的内存
            } else {
                // to big
                // 分配内存。内存从后面往前分配
                p->size -= (req_size + HEADER_SIZE);
                p = NEXT_HEADER(p);
                p->size = req_size;
            }
            free_list = prevp;
            FL_SET(p, FL_ALLOC);    // 设置当前p地址的flag为FL_ALLOC(已分配)
            return (void*) (p + 1);
        }

        // 如果p等于free_list，内存从后往前分配内存，已经分配完所有内存
        if (p == free_list) {
            if (!do_gc) {   // 执行GC操作
                garbage_collect();
                do_gc = 1;
            } else if ((p = grow(req_size)) == NULL) {
                return NULL;
            }
        }
    }
}

void mini_gc_free(void *ptr) {
    Header *target, *hit;

    target = (Header *)ptr - 1;

    // 搜索目标到free_list的连接点
    for (hit = free_list; !(target > hit && target < hit->next_free); hit = hit->next_free) {
        // heap end? And hit(search)
        // 寻找ptr在free_list中的位置，然后进行下一步操作
        if (hit >= hit->next_free && (target > hit || target < hit->next_free)) {
            break;
        }
    }

    // 如果target地址大于hit地址，为target增加size和next_free
    if (NEXT_HEADER(target) ==  hit->next_free) {
        // merge
        target->size += (hit->next_free->size + HEADER_SIZE);
        target->next_free = hit->next_free->next_free;
    } else {
        // join next free block
        target->next_free = hit->next_free;
    }

    // 如果hit地址大于target地址. 为hit增加size和next_free
    if (NEXT_HEADER(hit) == target) {
        // merge
        hit->size += (target->size + HEADER_SIZE);
        hit->next_free = target->next_free;
    } else {
        // join before free block
        hit->next_free = target;
    }

    free_list = hit;
    // 初始化flags,重新然后内存可以再分配
    target->flags = 0;
}

static GC_Heap* is_pointer_to_heap(void *ptr) {
    size_t i;
    /**
     * 意思是ptr是否在hit_cache->slot已经分配的内存区间内
     */
    if (hit_cache && ((void *)hit_cache->slot) <= ptr && (size_t)ptr < (((size_t)hit_cache->slot) + hit_cache->size)) {
        return hit_cache;
    }

    for (i = 0; i < gc_heaps_used; i++) {
        /**
         * 判断gc_heaps[i].slot内存分配从低地址往高地址分配
         * 这里的意思是ptr是否在gc_heaps[i].slot已经分配的内存区间内
         */
        if ((((void *)gc_heaps[i].slot) <= ptr) && ((size_t)ptr < (((size_t)gc_heaps[i].slot) + gc_heaps[i].size))) {
            hit_cache = &gc_heaps[i];
            return &gc_heaps[i];
        }
    }

    return NULL;
}

/**
 * 获取头信息
 */
static Header* get_header(GC_Heap *gh, void *ptr) {
    Header *p, *pend, *pnext;

    pend = (Header *)(((size_t)gh->slot) + gh->size);
    for (p = gh->slot; p < pend; p = pnext) {
        pnext = NEXT_HEADER(p);
        // 确定内存的范围
        if ((void *)(p + 1) <= ptr && ptr < (void *)pnext) {
            return p;
        }
    }

    return NULL;
}


void gc_init(void) {
    long dummy;

    dummy = 42;

    // 检查stack增长
    stack_start = ((void*)&dummy);
}

static void set_stack_end(void) {
    void *tmp;
    long dummy;

    // referenced bdw-gc mark_rts.c
    dummy = 42;

    stack_end = (void *)&dummy;
}


// 标记阶段
static void gc_mark(void *ptr) {
    GC_Heap *gh;
    Header *hdr;

    // mark check
    if (!(gh = is_pointer_to_heap(ptr))) return;
    // 获取头信息
    if (!(hdr = get_header(gh, ptr))) return;
    // 检测内存是否分配
    if (!FL_TEST(hdr, FL_ALLOC)) return;
    // 检测是否被标记。如果被标记了，就返回
    if (FL_TEST(hdr, FL_MARK)) return;

    // marking. 标记. FL_ALLOC | FL_MARK = 3
    FL_SET(hdr, FL_MARK);
    DEBUG(printf("mark ptr: %p, header: %p\n", ptr, hdr));

    // mark children.标记子节点
    gc_mark_range((void *)(hdr + 1), (void *)NEXT_HEADER(hdr));
}

// 范围标记
static void gc_mark_range(void *start, void *end) {
    void *p;

    for (p = start; p < end; p++) {
        gc_mark(*(void **)p);
    }
}

static void gc_mark_register(void) {
    jmp_buf env;
    size_t i;

    setjmp(env);
    // setjmp是怎么工作的: https://zhuanlan.zhihu.com/p/82492121
    for (i = 0; i < sizeof(env); i++) {
        gc_mark(((void **)env)[i]);
    }
}

static void gc_mark_stack(void) {
    set_stack_end();
    // 扫描stack
    if (stack_start > stack_end) {
        gc_mark_range(stack_end, stack_start);
    } else {
        gc_mark_range(stack_start, stack_end);
    }
}

// 清除阶段
static void gc_sweep(void) {
    size_t i;
    Header *p, *pend, *pnext;

    // 遍历gc_heaps数组
    for (i = 0; i < gc_heaps_used; i++) {
        pend = (Header *)(((size_t)gc_heaps[i].slot) + gc_heaps[i].size);
        for (p = gc_heaps[i].slot; p < pend; p = NEXT_HEADER(p)) {
            // 如果已经被分配内存
            if (FL_TEST(p, FL_ALLOC)) {
                // 如果已经被标记
                if (FL_TEST(p, FL_MARK)) {
                    DEBUG(printf("mark unset: %p\n", p));
                    // 取消标记
                    FL_UNSET(p, FL_MARK);
                } else {
                    // 释放内存
                    mini_gc_free(p + 1);
                }
            }
        }
    }
}

void add_roots(void *start, void *end) {
    void *tmp;
    if (start > end) {
        tmp = start;
        start = end;
        end = tmp;
    }

    root_ranges[root_ranges_used].start = start;
    root_ranges[root_ranges_used].end = end;
    root_ranges_used++;

    if (root_ranges_used >= ROOT_RANGES_LIMIT) {
        fputs("Root OverFlow", stderr);
        abort();
    }
}

void garbage_collect(void) {
    size_t i;

    // marking machine context.标记机器上下文
    gc_mark_register();
    gc_mark_stack();

    // 标记根
    for (i = 0; i < root_ranges_used; i++) {
        gc_mark_range(root_ranges[i].start, root_ranges[i].end);
    }

    // sweeping
    gc_sweep();
}

/* ========================================================================== */
/*  test                                                                      */
/* ========================================================================== */
static void test_mini_gc_malloc_free() {
    void *p1, *p2, *p3;

    // malloc check
    p1 = (void *)mini_gc_malloc(10);
    p2 = (void *)mini_gc_malloc(10);
    p3 = (void *)mini_gc_malloc(10);

    assert(((Header*)p1 - 1)->size == ALIGN(10, PTRSIZE));
    assert(((Header *)p1 - 1)->flags == FL_ALLOC);
    assert((Header *)(((size_t)(free_list + 1)) + free_list->size) == ((Header*)p3 - 1));

    // free check
    mini_gc_free(p1);
    mini_gc_free(p3);
    mini_gc_free(p2);

    assert(free_list->next_free == free_list);
    assert((void *)gc_heaps[0].slot == (void *)free_list);
    assert(gc_heaps[0].size == TINY_HEAP_SIZE);
    assert(((Header *)p1 - 1)->flags == 0);

    // grow check
    p1 = mini_gc_malloc(TINY_HEAP_SIZE + 80);
    assert(gc_heaps_used == 2);
    assert(gc_heaps[1].size == (TINY_HEAP_SIZE + 80));
    mini_gc_free(p1);
}

static void test_garbage_collect() {
    void *p;
    p = mini_gc_malloc(100);
    assert(FL_TEST((((Header *)p)-1), FL_ALLOC));
    p = 0;
    garbage_collect();
}

static void test_garbage_collect_load_test() {
    void *p;
    int i;
    for (i = 0; i < 2000; i++) {
        p = mini_gc_malloc(100);
    }

    assert((((Header *)p)-1)->flags);
    assert(stack_end != stack_start);
}

int main() {
    gc_init();
    test_mini_gc_malloc_free();
    test_garbage_collect();
    test_garbage_collect_load_test();
    return 0;
}