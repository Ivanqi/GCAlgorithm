#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "reference_counting.h"

node *next_free;
node *head;
int _rp;

/**
 * @brief 回收对象
 * 
 * @param obj 
 */
void reclaim(object* obj);

/**
 * @brief 增加计数，不考虑上溢情况
 * 
 * @param obj 被引用的对象
 */
void inc_ref_cnt(object *obj);

/**
 * @brief 计数器自减
 * 
 * @param obj 原引用
 */
void dec_ref_cnt(object *obj);

int resolve_heap_size(int size);

/**
 * @brief 查找空闲内存单元
 * 
 * @return node* 
 */
node *find_idle_node();

void reclaim(object* obj) {
    // 回收对象所属的node
    memset(obj, 0, obj->clss->size);

    // 通过地址计算出，对象所在的node
    node* _node = (node*) ((void *) obj - sizeof(node));
    _node->used = FALSE;
    _node->data = NULL;
    _node->size = 0;

    // 将next_free更新为当前回收的node
    next_free = _node;
    printf("collection ...\n");
}

void inc_ref_cnt(object* obj) {
    if (!obj) {
        return;
    }

    obj->ref_cnt++;
}

void dec_ref_cnt(object* obj) {
    if (!obj) {
        return;
    }

    obj->ref_cnt--;
    // 如果计数器为0，则对象需要被回收，那么该对象引用的对象计数器都需要减少
    if (obj->ref_cnt == 0) {
        for (int i = 0; i < obj->clss->num_fields; ++i) {
            dec_ref_cnt(*((object **) ((void *) obj + obj->clss->field_offsets[i])));
        }
        // 回收
        reclaim(obj);
    }
}

/**
 * @brief gc_update_ptr里，可不可以先做减，后做加呢？
 *  1. 答案是不行。这是为了保证，当obj和value是同一个对象的时候
 *  2. 如果先减后加的话，那么这个对象就会被回收，内存有可能会被破坏
 * 
 * @param ptr 
 * @param obj 
 */
void gc_update_ptr(object** ptr, void* obj) {
    inc_ref_cnt(obj);
    dec_ref_cnt(*ptr);
    *ptr = obj;
}

void gc_add_root(void* obj) {
    inc_ref_cnt((object *) obj);
}

void gc_remove_root(void* obj) {
    dec_ref_cnt((object *) obj);
}

int resolve_heap_size(int size) {
    if (size > MAX_HEAP_SIZE) {
        size = MAX_HEAP_SIZE;
    }

    if (size < NODE_SIZE) {
        return NODE_SIZE;
    }

    return size / NODE_SIZE * NODE_SIZE;
}

node* init_free_list(int free_list_size) {
    node *head = NULL;

    for (int i = 0; i < free_list_size; ++i) {
        node *_node = (node *) malloc(NODE_SIZE);
        _node->next = head;
        _node->size = NODE_SIZE;
        _node->used = FALSE;
        _node->data = NULL;
        head = _node;
    }

    return head;
}

void gc_init(int size) {
    int heap_size = resolve_heap_size(size);
    int free_list_size = heap_size / NODE_SIZE;
    head = init_free_list(free_list_size);

    next_free = head;
}

node *find_idle_node() {
    for (next_free = head; next_free && next_free->used; next_free = next_free->next) {}

    //再找不到真的没了……
    if (!next_free) {
        printf("Allocation Failed!OutOfMemory...\n");
        abort();
    }
}

object* gc_alloc(class_descriptor* clss) {

    if (!next_free || next_free->used) {
        find_idle_node();
    }

    //赋值当前freePoint
    node* _node = next_free;

    //新分配的对象指针
    //将新对象分配在free-list的节点数据之后，node单元的空间内除了sizeof(node)，剩下的地址空间都用于存储对象
    object* new_obj = (void *) _node + sizeof(node);
    new_obj->clss = clss;
    new_obj->ref_cnt = 0;

    _node->used = TRUE;
    _node->data = new_obj;
    _node->size = clss->size;

    for (int i = 0; i < new_obj->clss->num_fields; ++i) {
        //*(data **)是一个dereference操作，拿到field的pointer
        //(void *)o是强转为void* pointer，void*进行加法运算的时候就不会按类型增加地址
        *(object **) ((void *) new_obj + new_obj->clss->field_offsets[i]) = NULL;
    }

    next_free = next_free->next;

    return new_obj;
}