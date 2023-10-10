#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "generational.h"

object* _roots[MAX_ROOTS];

object* _rs[MAX_ROOTS];

int _rp;
int _rsp;

void* heap;     // 堆指针
void* new;      // 新生代指针
void* new_eden; // 新生代eden指针
void* new_from; // 新生代from指针
void* new_to;   // 新生代to指针
void* old;      // 老年代

int next_forwarding_offset; // 复制目标区域的free pointer
int next_free_offset;       // 下一个空闲内存地址（相对位置）
int heap_size;              // 堆容量
int new_size;               // 新生代容量
int old_size;               // 老年代容量
int eden_size;              // eden区容量
int survivor_size;          // survivor区容量

object* _roots[MAX_ROOTS];

node* old_next_free;    // 老年代下一个空闲单元
node* old_head;         // 老年代free-list的头节点

void new_copying();     //  新生代复制

object* new_copy(object* obj);      // 新生代复制对象

void adjust_ref();                  // 复制后更新引用
int resolve_heap_size(int size);
void swap(void** src,void** dst);   // 指针交换
void minor_gc();                    // 新生代gc
void major_gc();                    // 老年代gc
void promotion(object* obj);        // 对象晋升

object* new_malloc(class_descriptor* class);    // 新生代分配
object* old_malloc(object* obj);                // 老年代分配
node* old_init_free_list(int free_list_size);   // 老年代free-list分配
node* old_find_idle_node();                     // 老年代free-list查找空闲单元

void old_mark(object* obj); // 老年代标记
void old_sweep();           // 老年代清除
void write_barrier(object* obj, object** field_ref, object* new_obj); // 写入屏障

/**
 * @brief 老年代free-list分配
 *  1. 老年区按NODE_SIZE 划分节点，节点之间构建链表
 * 
 * @param free_list_size 
 * @return node* 返回第一个老年区节点
 */
node* old_init_free_list(int free_list_size) {
    node *head = NULL;
    for (int i = 0; i < free_list_size; ++i) {
        node* _node = (node *)(i * NODE_SIZE + old);
        _node->next = head;
        _node->size = NODE_SIZE;
        _node->used = FALSE;
        _node->data = NULL;
        head = _node;
    }

    return head;
}

/**
 * @brief 老年代free-list查找空闲单元
 * 
 * @return node* 
 */
node* old_find_idle_node() {
    for (old_next_free = old_head; old_next_free && old_next_free->used; old_next_free = old_next_free->next) {}

    // 还找不到就触发回收
    if (!old_next_free) {
        printf("[Old]Allocation Failed. execute gc...\n");
        major_gc();
    }

    for (old_next_free = old_head; old_next_free && old_next_free->used; old_next_free = old_next_free->next) {}

    // 真的找不到
    if (!old_next_free) {
        printf("[Old]Allocation Failed!OutOfMemory...\n");
        abort();
    }
}

/**
 * @brief 堆数据分配
 *  1. 新生代空间：幸存空间 + 生成空间 
 *  2. 老年代空间
 * 
 * @param size 
 * @return int 
 */
int resolve_heap_size(int size) {
    if (size > MAX_HEAP_SIZE) {
        size = MAX_HEAP_SIZE;
    }

    // 处理小数问题
    new_size = size / 10 * NEW_RATIO;

    eden_size = new_size / 10 * SURVIVOR_RATIO;

    survivor_size = new_size / 10;

    // 新生代空间：幸存空间 + 生成空间
    new_size = eden_size + survivor_size * 2;

    // 老年代空间
    old_size = (size - new_size) / NODE_SIZE * NODE_SIZE;

    heap_size = new_size + old_size;

    return heap_size;
}

/**
 * @brief gc初始化
 * 
 * @param size 
 */
void gc_init(int size) {
    heap_size = resolve_heap_size(size);

    // 分配
    heap = malloc(heap_size);

    // 初始化各区域指针
    new = heap;

    new_eden = heap;

    // 幸存空间from区域
    new_from = eden_size + new_eden;

    // 幸存空间to区域
    new_to = survivor_size + new_from;

    // 老年代区
    old = new_size + heap;

    // 初始化老年代资源
    int free_list_size = old_size / NODE_SIZE;

    // 返回第一个老年区节点
    old_head = old_init_free_list(free_list_size);

    old_next_free = old_head;

    _rp = 0;
}

/**
 * @brief 内存分配 && 新生代GC
 * 
 * @param clss 
 * @return object* 
 */
object* new_malloc(class_descriptor* clss) {
    // 检查是否可以分配
    if (next_free_offset + clss->size > eden_size) {
        printf("[New]Allocation Failed. execute gc...\n");
        minor_gc();
        if (next_free_offset + clss->size > eden_size) {
            printf("[New]Allocation Failed! OutOfMemory...\n");
            abort();
        }
    }

    int old_offset = next_free_offset;

    // 分配后，free移动至下一个可分配位置
    next_free_offset = next_free_offset + clss->size;

    // 分配
    object* new_obj = (object *) (old_offset + new_eden);

    // 初始化
    new_obj->clss = clss;
    new_obj->forwarded = FALSE;
    new_obj->marked = FALSE;
    new_obj->age = 0;
    new_obj->forwarding = NULL;

    for (int i = 0; i < new_obj->clss->num_fields; ++i) {
        // *(data **)是一个dereference操作，拿到field的pointer
        // (void *)o是强转为void* pointer，void*进行加法运算的时候就不会按类型增加地址
        *(object **) ((void *) new_obj + new_obj->clss->field_offsets[i]) = NULL;
    }

    return new_obj;
}

/**
 * @brief 老年代内存分配
 * 
 * @param obj 
 * @return object* 返回老年代中的新内存
 */
object* old_malloc(object* obj) {
    // 如果内存不够，就开始老年代GC
    if (!old_next_free || old_next_free->used) {
        old_find_idle_node();
    }

    class_descriptor* clss = obj->clss;
    // 赋值当前freePoint
    node* _node = old_next_free;

    // 新分配的对象指针
    // 将新对象分配在free-list的节点数据之后，node单元的空间除了sizeof(node), 剩下的地址空间都用于存储对象
    object* new_obj = (void*) _node + sizeof(node);

    // 复制
    memcpy(new_obj, obj, obj->clss->size);

    new_obj->forwarded = FALSE;
    new_obj->marked = FALSE;

    _node->used = TRUE;
    _node->data = new_obj;
    _node->size = clss->size;

    for (int i = 0; i < new_obj->clss->num_fields; ++i) {
        // *(data **)是一个dereference操作，拿到field的pointer
        // (void *)o是强转为void* pointer，void*进行加法运算的时候就不会按类型增加地址
        *(object **) ((void *) new_obj + new_obj->clss->field_offsets[i]) = NULL;
    }

    old_next_free = old_next_free->next;

    return new_obj;
}

/**
 * @brief 内存分配
 * 
 * @param clss 
 * @return object* 
 */
object* gc_alloc(class_descriptor* clss) {
    object* new_obj = new_malloc(clss);
    return new_obj;
}

/**
 * @brief 将对象复制到to
 *  1. 为了实现简单，此处不考虑to区无法存放的问题
 * 
 * @param obj 
 * @return object* 复制后的对象指针
 */
object* new_copy(object* obj) {
    if (!obj) { return NULL; }

    // 由于一个对象可能被多个对象引用，所以此处判断，避免重复复制
    if (!obj->forwarded) {
        // 新生代
        if (obj->age < MAX_AGE) {
            // 计算复制后的指针
            if (next_free_offset + obj->clss->size > survivor_size) {
                printf("[New]Copy failed! Insufficient TO space\n");
                abort();
            }

            // 增加年龄
            obj->age++;

            // 将幸存空间做为目标空间，进行复制
            object* forwarding = (object* )(next_forwarding_offset + new_to);

            // 复制
            memcpy(forwarding, obj, obj->clss->size);

            forwarding->forwarded = FALSE;

            forwarding->remembered = FALSE;

            obj->forwarded = TRUE;

            // 将复制后的指针，写入原对象的forwarding pointer,为最后更新引用做准备
            obj->forwarding = forwarding;

            // 复制后，移动to区forwarding偏移
            next_forwarding_offset += obj->clss->size;

            // 递归复制引用对象，递归是深度优先
            for (int i = 0; i < obj->clss->num_fields; ++i) {
                new_copy(*(object **) ((void *) obj + obj->clss->field_offsets[i]));
            }
        } else {
            // 超过年龄就晋升
            promotion(obj);
        }
    }

    return obj->forwarding;
}

void swap(void** src, void** dst) {
    object *temp = *src;
    *src = *dst;
    *dst = temp;
}

/**
 * @brief 复制后更新引用
 * 
 */
void adjust_ref() {
    int p = 0;
    // 遍历to，即复制的目标空间
    while (p < next_forwarding_offset) {
        object* obj = (object *) (p + new_to);
        // 将还指向from的引用更新为forwarding pointer，即to中的pointer
        for (int i = 0; i < obj->clss->num_fields; ++i) {
            object** field = (object **) ((void *)obj + obj->clss->field_offsets[i]);
            if ((*field) && (*field)->forwarding) {
                *field = (*field)->forwarding;
            }
        }

        // 顺序访问下一个对象
        p = p + obj->clss->size;
    }
}

/**
 * @brief 修改引用
 * 
 * @param obj 
 * @param field_ref 
 * @param new_obj 
 */
void gc_update_ptr(object* obj, object** field_ref, object* new_obj) {
    write_barrier(obj, field_ref, new_obj);
}

/**
 * @brief 写入屏障
 * 
 * @param obj 
 * @param field_ref 
 * @param new_obj 
 */
void write_barrier(object* obj, object** field_ref, object* new_obj) {

    /**
     * 为了将老年代对象记录到记录集里
     *  1. 发出引用的对象是不是老年代对象
     *  2. 指针更新后的引用的目标对象是不是新生代对象
     *  3. 发出引用的对象是否还没有被记录到记录集中
    */
    if ((void *)obj >= old && (void*) new_obj <= new_eden + eden_size + survivor_size && !obj->remembered) {
        _rs[_rsp++] = obj;
    }

    *field_ref = new_obj;
}

/**
 * @brief 新生代gc
 * 
 */
void minor_gc() {
    printf("minor gc\n");
    next_forwarding_offset = 0;

    // 遍历GC ROOTS
    for (int i = 0; i < _rp; ++i) {
        object* root = _roots[i];

        // 只处理处于新生代中的root，地址小于to的部分
        if ((void *)root <= (void *)new_eden + eden_size + survivor_size) {
            object* forwarding = new_copy(root);

            // 先将GC ROOTS引用的对象更新到to空间的新对象
            _roots[i] = forwarding;
        }
    }

    int i = 0;
    // 找到rs中的跨代引用
    while (i < _rsp) {
        object* old_root = _rs[i];
        byte has_new_obj = FALSE;

        for (int i = 0; i < old_root->clss->num_fields; ++i) {
            object** new_object_p = (object**)((void *) old_root + old_root->clss->field_offsets[i]);
            object* new_object = *new_object_p;

            if ((void *)new_object < (void *)old) {
                object* forwarding = new_copy(new_object);

                // 将老年代对新生代的引用更新为复制之后的对象
                *new_object_p = forwarding;

                if ((void *) forwarding < (void *)old) {
                    has_new_obj = TRUE;
                }
            }
        }

        // 如果该老年代对象引用的所有新生代对象都已经晋升到老年代，则删除rs中这个记录
        if (!has_new_obj) {
            _rs[i]->remembered = FALSE;

            // 如果不是最后一个元素，就先交换
            if (i < _rsp - 1) {
                swap((void **)&_rs[i], (void **)(&_rs[_rsp]));
            }

            // 移除最后一个
            _rs[--_rsp] = NULL;
        } else {
            i++;
        }
    }

    // 更新引用
    adjust_ref();

    // 清空Eden/from
    next_free_offset = 0;
    memset(new_eden, 0, eden_size);
    memset(new_from, 0, survivor_size);

    swap((void **)&new_from, (void **)&new_to);

}

/**
 * @brief 对象晋升
 * 
 * @param obj 
 */
void promotion(object* obj) {
    printf("promotion...\n");
    object* new_obj = old_malloc(obj);
    obj->forwarding = new_obj;
    obj->forwarded = TRUE;

    for (int i = 0; i < new_obj->clss->num_fields; ++i) {
        object* ref_obj = *(object **) ((void*) new_obj + new_obj->clss->field_offsets[i]);

        // 如果晋升后的对象还引用着新生代对象，则记录再rs中
        if (ref_obj && (void*)ref_obj <= (void *)old) {
            new_obj->remembered = true;
            _rs[_rsp++] = new_obj;
            break;
        }
    }
}

/**
 * @brief 老年代gc
 * 
 */
void major_gc() {
    for (int i = 0; i < _rp; ++i) {
        // 遍历老年区的节点
        object* root = _roots[i];
        if ((void *)root > old) {
            old_mark(_roots[i]);
        }
    }
    old_sweep();
}

/**
 * @brief 老年代标记
 * 
 * @param obj 
 */
void old_mark(object* obj) {
    // 递归剪枝
    if (!obj || obj->marked) { return; }

    obj->marked = TRUE;
    printf("marking...\n");

    // 递归标记对象的引用
    for (int i = 0; i < obj->clss->num_fields; ++i) {
        // 只对引用的老年代部分对象标记，major gc只回收老年代
        object* ref_obj = *((object **) ((void *) obj + obj->clss->field_offsets[i]));
        if ((void *)ref_obj >= old) {
            old_mark(ref_obj);
        }
    }
}

/**
 * @brief 老年代清除
 * 
 */
void old_sweep() {
    int i = 0, step = 0;

    for (node* _cur = old_head; _cur && _cur; _cur = _cur->next) {
        if (!_cur->used) continue;
        object* obj = _cur->data;
        
        if (obj->marked) {
            obj->marked = FALSE;
        } else {
            // 回收对象所属的node
            memset(obj, 0, obj->clss->size);

            // 通过地址计算出，对象所在的node
            node* _node = (node*) ((void *) obj - sizeof(node));
            _node->used = FALSE;
            _node->data = NULL;
            _node->size = 0;

            // 将next_free更新为当前回收的node
            old_next_free = _node;
            printf("collection ...\n");
        }
    }
}

/**
 * @brief 获取GC状态
 * 
 * @return char* 
 */
char* gc_get_state() {
    printf("Heap Usage:\n");
    printf("New Generation\n");
    printf("Eden Space:\n");
    printf("   capacity = %d\n", eden_size);
    printf("   used     = %d\n", next_free_offset);
    printf("   free     = %d\n", eden_size - next_free_offset);
    printf("   %g%% used\n", (double)next_free_offset / eden_size*100);
    printf("From Space:\n");
    printf("   capacity = %d\n", survivor_size);
    printf("   used     = %d\n", next_forwarding_offset);
    printf("   free     = %d\n", survivor_size - next_forwarding_offset);
    printf("   %g%% used\n", (double)next_forwarding_offset / survivor_size * 100);
    printf("To Space:\n");
    printf("   capacity = %d\n",survivor_size);
    printf("   used     = %d\n",0);
    printf("   free     = %d\n",survivor_size);
    printf("   %g%% used\n", 0);
    printf("Old Generation\n");

    int old_used = 0;
    for (node* _n = old_head; _n; _n = _n->next) {
        if (_n->used) {
            old_used += NODE_SIZE;
        }
    }

    printf("   capacity = %d\n", old_size);
    printf("   used     = %d\n", old_used);
    printf("   free     = %d\n", old_size - old_used);
    printf("   %g%% used\n", (double)old_used / old_size * 100);

    return NULL;
}

void gc() {
    minor_gc();
    major_gc();
}