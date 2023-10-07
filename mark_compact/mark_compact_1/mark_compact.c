#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mark_compact.h"

object* _roots[MAX_ROOTS];

int _rp;                    // GC ROOTS index
void* heap;                 // 堆指针
object* _roots[MAX_ROOTS];  // GC ROOTS
int next_free_offset;       // 下一个空闲内存地址（相对位置）
int heap_size;              // 堆容量

// 计算并更新forwarding pointer
void set_forwarding();

// 调整存活对象的引用指针
void adjust_ref();

// 移动存活对象至forwarding pointer
void move_obj();

// 整理
void compact();

// 标记
void mark(object* obj);

int resolve_heap_size(int size);

/**
 * @brief 处理初始值
 * 
 * @param size 初始堆大小
 * @return int 一半堆的大小
 */
int resolve_heap_size(int size) {
    if (size > MAX_HEAP_SIZE) {
        size = MAX_HEAP_SIZE;
    }
    return size;
}

void gc_init(int size) {
    heap_size = resolve_heap_size(size);
    heap = (void *) malloc(heap_size);
    _rp = 0;
}

object* gc_alloc(class_descriptor* clss) {
    // 检查是否可以分配
    if (next_free_offset + clss->size > heap_size) {
        printf("Allocation Failed. execute gc ...\n");
        gc();
        if (next_free_offset + clss->size > heap_size) {
            printf("Allocation Failed! OutOfMemory...\n");
            abort();
        }
    }

    int old_offset = next_free_offset;

    // 分配后，free移动至下一个可分配位置
    next_free_offset = next_free_offset + clss->size;

    //分配
    object* new_obj = (object *) (old_offset + heap);

    // 初始化
    new_obj->clss = clss;
    new_obj->marked = FALSE;
    new_obj->forwarding = NULL;

    for (int i = 0; i < new_obj->clss->num_fields; ++i) {
        // *(data **)是一个dereference操作，拿到field的pointer
        // (void *)o是强转为void* pointer，void*进行加法运算的时候就不会按类型增加地址
        *(object **) ((void *) new_obj + new_obj->clss->field_offsets[i]) = NULL;
    }

    return new_obj;
}

/**
 * @brief 设定forwarding指针
 *  1. 程序首先会搜索整个堆，给活动对象设定forwarding指针
 *  2. 对象obj的forwarding指针可以用obj.forwarding来访问
 * @attention
 *  1. 在 GC 标记 - 压缩算法中新空间和原空间是同一个空间，所以有可能出现把移动前的对象覆盖掉的情况
 *  2. 因此在移动对象前，需要事先将各对象的指针全部更新到预计要移动到的地址
 */
void set_forwarding() {
    int scan = 0;           // scan 是用来搜索堆中的对象的指针
    int new_address = 0;    // new_address 是指向目标地点的指针

    // 遍历堆的已使用部分，这里不用遍历全堆
    // 因为是顺序分配法，所以只需要遍历到已分配的终点即可
    // 一旦 scan 指针找到活动对象，就会将对象的 forwarding 指针的引用目标从 NULL 更新到
    // new_address，将 new_address 按对象长度移动
    while (scan < next_free_offset) {
        object* obj = (object *)(scan + heap);

        // 为可达的对象设置forwarding
        if (obj->marked) {
            obj->forwarding = (object *)(new_address + heap);
            new_address = new_address + obj->clss->size;
        }

        scan = scan + obj->clss->size;
    }
}

/**
 * @brief 更新指针
 * 
 */
void adjust_ref() {
    int scan = 0;

    // 先将roots的引用更新。重写根的指针
    for (int i = 0; i < _rp; ++i) {
        object* r_obj = _roots[i];
        _roots[i] = r_obj->forwarding;
    }

    // 再遍历堆，更新存活对象的引用
    while (scan < next_free_offset) {
        object* obj = (object *)(scan + heap);

        if (obj->marked) {
            // 更新引用为forwarding
            for (int i = 0; i < obj->clss->num_fields; ++i) {
                object** field = (object **) ((void *) obj + obj->clss->field_offsets[i]);
                if ((*field) && (*field)->forwarding) {
                    *field = (*field)->forwarding;
                }
            }
        }

        scan = scan + obj->clss->size;
    }
}

/**
 * @brief 移动对象
 *  1. 搜索整个堆，将活动对象移动到 forwarding 指针的引用目标处
 * 
 */
void move_obj() {
    int scan = 0;
    int new_next_free_offset = 0;

    while (scan < next_free_offset) {
        object* obj = (object *) (scan + heap);

        if (obj->marked) {
            // 移动对象至forwarding
            obj->marked = FALSE;
            memcpy(obj->forwarding, obj, obj->clss->size);
            new_next_free_offset = new_next_free_offset + obj->clss->size;
        }

        scan = scan + obj->clss->size;
    }

    // 清空移动后的间隙
    memset((void *)(new_next_free_offset + heap), 0, next_free_offset - new_next_free_offset);

    // 移动完成后，更新free pointer为新的边界指针
    next_free_offset = new_next_free_offset;
}

void compact() {
    set_forwarding();
    adjust_ref();
    move_obj();
}

void mark(object* obj) {
    if (!obj || obj->marked) { return; }

    obj->marked = TRUE;
    printf("marking...\n");

    // 递归标记对象的引用
    for (int i = 0; i < obj->clss->num_fields; ++i) {
        mark(*((object **) ((void *) obj + obj->clss->field_offsets[i])));
    }
}

void gc() {

    for (int i = 0; i < _rp; ++i) {
        mark(_roots[i]);
    }

    compact();
}