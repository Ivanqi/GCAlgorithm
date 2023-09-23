#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int marked;
} Object;

void mark(Object* obj) {
    obj->marked = 1;
}

void sweep(Object* objects, int numObjects) {
    for (int i = 0; i < numObjects; i++) {
        if (!objects[i].marked) {
            // 清除未标记的对象，这里可以是释放内存的操作
        } else {
            objects[i].marked = 0;  // 将标记位重置为未标记
        }
    }
}

void gc(Object* objects, int numObjects) {
    // 标记阶段
    for (int i = 0; i < numObjects; i++) {
        mark(&objects[i]);
    }

    // 清除阶段（延迟清除）
    sweep(objects, numObjects);
}

/**
 * @brief 延迟清除法（Lazy Sweep）
 *  1. 延迟清除法（Lazy Sweep）是标记清除算法的一种优化策略
 *  2. 在传统的标记清除算法中，清除阶段会遍历整个内存空间，将未标记的对象进行清除
 *  3. 这种方式会导致垃圾回收的停顿时间较长，因为清除阶段需要遍历整个堆空间
 *  4. 而延迟清除法则是将清除操作延迟到下一次的分配阶段
 *  5. 在标记阶段，只进行标记，不进行清除操作
 *  6. 当需要分配内存时，如果发现当前的内存空间不够用，则进行清除操作，将未标记的对象进行清除，并将空闲的内存块归还到空闲列表中
 * 
 * @return int 
 */
int main() {
    Object objects[5];
    
    // 在这里模拟对象的引用关系和标记
    objects[0].marked = 1;
    objects[1].marked = 0;
    objects[2].marked = 1;
    objects[3].marked = 0;
    objects[4].marked = 1;
    
    gc(objects, 5);
    
    return 0;
}