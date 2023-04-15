#include <stdlib.h>
#include <stdio.h>

#define HEAP_SIZE 10000

typedef struct _gc_obj_t {
    int marked;
    struct _gc_obj_t* next;
} gc_obj_t;

gc_obj_t* heap[HEAP_SIZE];
gc_obj_t* free_list;

void gc_init() {
    for (int i = 0; i < HEAP_SIZE; i++) {
        heap[i] = NULL;
    }
    free_list = NULL;
}

// 内存分配
gc_obj_t* gc_alloc() {
    // 初始化heap，构建链表，并设置free_list
    if (free_list == NULL) {
        for (int i = 0; i < HEAP_SIZE; i++) {
            if (heap[i] == NULL) {
                gc_obj_t* obj = (gc_obj_t*) malloc(sizeof(gc_obj_t));
                obj->marked = 0;
                obj->next = heap[i];
                heap[i] = obj;
                free_list = obj;
                break;
            }
        }
    }

    if (free_list == NULL) exit(1);

    gc_obj_t* obj = free_list;
    free_list = obj->next;
    obj->next = NULL;
    return obj;
}

void gc_free(gc_obj_t* obj) {
    obj->marked = 0;
    obj->next = free_list;
    free_list = obj;
}

// 标记活跃内存块
void gc_mark(gc_obj_t* obj) {
    if (obj == NULL || obj->marked) return;

    obj->marked = 1;
    gc_mark(obj->next);
}

// 清除非活跃内存块
void gc_sweep() {
    for (int i = 0; i < HEAP_SIZE; i++) {
        gc_obj_t* obj = heap[i];
        while (obj != NULL) {
            if (!obj->marked) {
                gc_obj_t* tmp = obj;
                obj = obj->next;
                gc_free(tmp);
            } else {
                obj->marked = 0;
                obj = obj->next;
            }
        }
    }
}

// 垃圾回收函数
void gc_collect() {
    gc_mark(free_list);
    gc_sweep();
}

/**
 * @brief 评价
 *  1. 吞吐量：由于这是一个简单的标记-清除算法，没有使用任何复杂的算法或并行化处理，因此吞吐量可能不高。
 *  2. 最大暂停时间：由于该算法需要在一次完整的垃圾回收中遍历整个堆，因此暂停时间可能比使用更高级的算法要长
 *  3. 堆使用效率：使用指针数组和空闲对象列表来实现堆，没有使用任何内存池等高级数据结构，因此堆使用效率可能不高
 *  4. 访问的局部性：由于该算法需要遍历整个堆，因此访问的局部性可能不太好。如果堆的大小足够小，那么缓存局部性可能会更好
 * 
 * @return int 
 */

int main() {
    gc_init();

    gc_obj_t* obj1 = gc_alloc();
    gc_obj_t* obj2 = gc_alloc();
    gc_obj_t* obj3 = gc_alloc();

    obj1->next = obj2;
    obj2->next = obj3;

    gc_collect();

    printf("Free list: %p\n", free_list); // expect: 0x55555576e3e0

    gc_obj_t* obj4 = gc_alloc();

    gc_collect();

    printf("Free list: %p\n", free_list); // expect: 0x55555576e3e0

    return 0;
}