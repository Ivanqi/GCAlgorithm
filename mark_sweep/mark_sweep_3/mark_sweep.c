#include <stdio.h>
#include <stdlib.h>

#define HEAP_SIZE 1024 // 内存池大小
#define MARKED 0x01    // 标记位

// 内存块结构体
typedef struct Node Node;
struct Node {
    Node* next;
    size_t size;
    int mark;
    char data[]; // 存放数据的数组
};

// 内存池空间
static char heap[HEAP_SIZE];

// 内存池的头结点
static Node* heap_head;

// 分配内存
void* allocate(size_t size) {
    Node* p = heap_head, *pre = NULL;
    void* result = NULL;
    while (p != NULL) {
        if (!p->mark && p->size >= size) {
            p->mark = MARKED; // 标记为活跃
            result = p->data;
            break;
        }
        pre = p;
        p = p->next;
    }

    if (result == NULL) {
        // 分配新内存块
        p = (Node*)malloc(size + sizeof(Node));
        p->next = NULL;
        p->size = size;
        p->mark = MARKED;

        // 将新内存块加入内存池尾部
        if (pre == NULL) {
            heap_head = p;
        } else {
            pre->next = p;
        }
    }

    return result;
}

// 清除内存
void collect() {
    Node* p = heap_head, *pre = NULL, *temp = NULL;

    // 标记阶段
    while (p != NULL) {
        if (p->mark) {
            p->mark = 0; // 重置标记
        }
        pre = p;
        p = p->next;
    }

    // 清除阶段
    p = heap_head;
    while (p != NULL) {
        if (!p->mark) {
            if (pre == NULL) { // 删除第一个节点
                heap_head = p->next;
            } else {
                pre->next = p->next;
            }
            temp = p->next;
            free(p);
            p = temp;
        } else {
            pre = p;
            p = p->next;
        }
    }

    // 合并阶段
    p = heap_head;
    while (p->next != NULL) {
        if ((char*)p + sizeof(Node) + p->size == (char*)p->next) { // 可以合并
            p->size += sizeof(Node) + p->next->size;
            p->next = p->next->next;
        } else {
            p = p->next;
        }
    }
}


int main() {
    int* p1 = (int*)allocate(sizeof(int));
    int* p2 = (int*)allocate(sizeof(int));
    int* p3 = (int*)allocate(sizeof(int));

    *p1 = 1;
    *p2 = 2;
    *p3 = 3;

    collect();

    int* p4 = (int*)allocate(sizeof(int));
    *p4 = 4;

    printf("%d %d %d %d\n", *p1, *p2, *p3, *p4);
    return 0;
}
