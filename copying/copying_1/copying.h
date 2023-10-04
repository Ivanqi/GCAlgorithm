#ifndef GC_IMPL_GC_H
#define GC_IMPL_GC_H

#endif

// 1字节的byte类型，用来做标识位
typedef unsigned char byte;

// 类描述
typedef struct class_descriptor {
    char *name;             // 类名称
    int size;               // 类大小，即对应sizeof(struct)
    int num_fields;         // 属性数量
    int *field_offsets;     // 类中的属性偏移，即所有属性在struct中的偏移量（字节）
} class_descriptor;

/**
 * @brief 基本对象类型
 *  1. 所有对象都继承于Object
 *  2. C中没有继承的概念，不过可以通过定义相同属性来实现，所有“继承”Object的struct，都需要将class/marked属性定义在开头
 * 
 */
typedef struct _object object;
struct _object {
    class_descriptor *clss; // 对象对应的类型
    byte forwarded;         // 已拷贝标识
    object *forwarding;     // 目标位置
};

#define MAX_ROOTS 100

#define MAX_HEAP_SIZE 1024 * 1024 * 10 // 10MB

const static byte TRUE = 1;
const static byte FALSE = 0;

// GC ROOTS
extern object *_roots[MAX_ROOTS];

// GC ROOT 的当前下标，即记录到了第几个元素
extern int _rp;

// 堆总大小
extern int heap_size;

/**
 * @brief 初始化GC
 * 
 * @param size 
 */
extern void gc_init(int size);

/**
 * @brief 执行GC
 * 
 */
extern void gc();

/**
 * @brief GC结束，彻底清理堆
 * 
 */
extern void gc_done();

/**
 * @brief 在GC堆上分配指定类型的内存
 *  1. 使用顺序分配(sequential allocation)内存的方式
 * 
 * @param clss 需要分配的类型
 * @return object* 分配的对象指针
 */
extern object *gc_alloc(class_descriptor *clss);

/**
 * @brief DUMP GC状态
 * 
 * @return char* 
 */
extern char *gc_get_state();

/**
 * @brief 获取GC ROOTS数量
 * 
 * @return int ROOTS数量
 */
extern int gc_num_roots();

// 暂存GC ROOTS下标
#define gc_save_rp int __rp = _rp;

// 将对象添加到GC ROOTS
#define gc_add_root(p) _roots[_rp++] = (object *)(p);

// 恢复GC ROOTS下标
#define gc_restore_roots _rp = __rp;