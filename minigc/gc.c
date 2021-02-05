#ifndef DO_DEBUG
#define DEBUG(exp) (exp)
#else
#define DEBUG(exp)
#endif

#ifndef DO_DEBUG
#define NDEBUG
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <setjmp.h>
#include "gc.h"

typedef struct header {
    size_t flags;
    size_t size;
    struct header *next_free;
} Header;

typedef struct gc_heap {
    Header *slot;
    size_t size;
} GC_Heap;

#define TINY_HEAP_SIZE 0x4000
#define ALIGN(x, a) (((x) + (a - 1)) & ~(a - 1))
#define NEXT_HEADER(x) ((Header *)((size_t)(x + 1) + x->size))
#define PTRSIZE ((size_t)sizeof(void *))
#define HEADER_SIZE ((size_t)sizeof(Header))
#define HEAP_LIMIT 10000

// flags
#define FL_ALLOC 0x1
#define FL_MARK 0x2
#define FL_SET(x, f) (((Header *)x)->flags |= f)
#define FL_UNSET(x, f) (((Header *)x)->flags &= ~(f))
#define FL_TEST(x, f) (((Header *)x)->flags & f)

static Header *free_list;
static GC_Heap gc_heaps[HEAP_LIMIT];
static size_t gc_heaps_used = 0;

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

    if ((prevp == free_list) == NULL) {
        if (!p = add_heap(TINY_HEAP_SIZE)) {
            return NULL;
        }
        prevp = free_list = p;
    }

    for (p = prevp->next_free; ; prevp = p, p = p->next_free) {
        if (p->size >= req_size) {
            if (p->size == req_size) {
                // just fit
                prevp->next_free = p->next_free;
            } else {
                // to big
                p->size -= (req_size + HEADER_SIZE);
                p = NEXT_HEADER(p);
                p->size = req_size;
            }
            free_list = prevp;
            FL_SET(p, FL_ALLOC);
            return (void*) (p + 1);
        }

        if (p == free_list) {
            if (!do_gc) {

            } else if ((p = grow(req_size)) == NULL) {
                return NULL;
            }
        }
    }
}


static void *stack_start = NULL;

void gc_init(void) {
    long dummy;

    dummy = 42;

    // 检查stack增长
    stack_start = ((void*)&dummy);
}

static void gc_mark_register(void) {
    
}  

void garbage_collect(void) {
    size_t i;

    // marking machine context.标记机器上下文

}