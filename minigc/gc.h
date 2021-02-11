#ifndef MINGC_H
#define MINGC_H

#ifndef DO_DEBUG
#define DEBUG(exp) (exp)
#else
#define DEBUG(exp)
#endif

#ifndef DO_DEBUG
#define NDEBUG
#endif


/* ========================================================================== */
/*  mini_gc_malloc                                                            */
/* ========================================================================== */
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

static Header* add_heap(size_t req_size);
static Header* grow(size_t req_size);
void* mini_gc_malloc(size_t req_size);
void mini_gc_free(void *ptr);


/* ========================================================================== */
/*  mini_gc                                                                   */
/* ========================================================================== */

struct root_range {
    void *start;
    void *end;
};

#define IS_MARKED(x) (FL_TEST(x, FL_ALLOC) && FL_TEST(x, FL_MARK))
#define ROOT_RANGES_LIMIT 1000

static struct root_range root_ranges[ROOT_RANGES_LIMIT];
static size_t root_ranges_used = 0;
static void *stack_start = NULL;
static void *stack_end = NULL;
static GC_Heap *hit_cache = NULL;

static GC_Heap* is_pointer_to_heap(void *ptr);
static Header* get_header(GC_Heap *gh, void *ptr);
void gc_init(void);
static void set_stack_end(void);
static void gc_mark_range(void *start, void *end);
static void gc_mark(void *ptr);
static void gc_mark_register(void);
static void gc_mark_stack(void);
static void gc_sweep(void);
void add_roots(void *start, void *end);
void garbage_collect(void);


#endif