#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "mark_sweep.h"

#define MAX_ROOTS 100

typedef struct dept {
    class_descriptor *class;    // 对象对应的类型
    byte marked;                // 标记对象是否可达（reachable）
    int id;
} dept;

typedef struct emp {
    class_descriptor *class;    // 对象对应的类型
    byte marked;                // 标记对象是否可达（reachable）
    int id;
    dept *dept;
} emp;

// C 库宏 offsetof(type, member-designator) 会生成一个类型为 size_t 的整型常量，它是一个结构成员相对于结构开头的字节偏移量。
// https://www.runoob.com/cprogramming/c-macro-offsetof.html
class_descriptor emp_object_class = {
    "emp_object",
    sizeof(struct emp),
    1,
    (int[]) {
        offsetof(struct emp, dept)
    }
};

class_descriptor dept_object_class = {
    "dept_object",
    sizeof(struct dept),                /* size of string obj, not string */
    0,                                  /* fields */
    NULL
};

int main(int argc, char *argv[]) {
    gc_init(256 * 3);

    for (int i = 0; i < 3; ++i) {
        printf("loop %d\n" ,i);

        emp *_emp1 = (emp *) gc_alloc(&emp_object_class);
        gc_add_root(_emp1);

        dept *_dept1 = (dept *) gc_alloc(&dept_object_class);
        _emp1->dept = _dept1;

        if (i == 2){
            printf("即将内存溢出\n");
        }

        emp *_emp2 = (emp *) gc_alloc(&emp_object_class);
        dept *_dept2 = (dept *) gc_alloc(&dept_object_class);
        
        _emp2->dept = _dept2;
    }
}