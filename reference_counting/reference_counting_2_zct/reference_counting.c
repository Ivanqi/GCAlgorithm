#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int ref_count;
    int is_live;
} Object;

typedef struct {
    Object* objects[100];
    int count;
} ZeroCountTable;

ZeroCountTable zct;

void init_object(Object* obj) {
    obj->ref_count = 0;
    obj->is_live = 0;
}

void increase_ref_count(Object* obj) {
    obj->ref_count++;
}

void decrease_ref_count(Object* obj) {
    obj->ref_count--;
    if (obj->ref_count == 0) {
        obj->is_live = 1;
        zct.objects[zct.count++] = obj;
    }
}

void collect_garbage() {
    for (int i = 0; i < zct.count; ++i) {
        Object* obj = zct.objects[i];
        if (obj->is_live) {
            free(obj);
        }
    }

    zct.count = 0;
}

/**
 * @brief 延迟引用计数法（Deferred Reference Counting）
 *  1. 延迟引用计数法（Deferred Reference Counting）是一种改进的引用计数法，在对象的引用计数器为0时，不立即回收对象
 *  2. 而是将其添加到一个特殊的数据结构中，通常称为Zero Count Table（ZCT），来延迟回收的时机
 *  3. 在延迟引用计数法中，当一个对象的引用计数减少到0时，不立即回收该对象，而是将其添加到ZCT中
 *  4. ZCT是一个记录了引用计数为0的对象的表格。然后，定期或在适当的时机，系统会遍历ZCT并回收其中的对象
 * 
 * @return int 
 */
int main() {
    Object* obj1 = (Object*)malloc(sizeof(Object));
    init_object(obj1);
    
    Object* obj2 = (Object*)malloc(sizeof(Object));
    init_object(obj2);
    
    increase_ref_count(obj1);
    increase_ref_count(obj2);
    
    decrease_ref_count(obj1);
    decrease_ref_count(obj2);
    
    collect_garbage();
    
    return 0;
}