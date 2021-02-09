#ifndef MINGC_H
#define MINGC_H

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



void mini_gc_free(void *ptr);
void *mini_gc_malloc(size_t req_size);

void garbage_collect(void);
void gc_init();
void add_roots(void *start, void *end);


#endif