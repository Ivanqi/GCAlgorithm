#include <stdio.h>
#include <stdlib.h>

// 定义空闲链表节点
typedef struct FreeListNode {
    struct FreeListNode* next;  // 指向下一个节点的指针
    size_t size;                // 分块大小
} FreeListNode;

// 定义空闲链表
typedef struct FreeList {
    FreeListNode* head;         // 链表的头节点
} FreeList;

// 初始化空闲链表
void initFreeList(FreeList* list) {
    list->head = NULL;
}

// 向空闲链表中插入一个节点
void insertFreeListNode(FreeList* list, size_t size) {
    FreeListNode* newNode = (FreeListNode*)malloc(sizeof(FreeListNode));
    newNode->size = size;
    newNode->next = list->head;
    list->head = newNode;
}

// 从空闲链表中删除一个节点
void removeFreeListNode(FreeList* list, FreeListNode* node) {
    if (list->head == node) {
        list->head = node->next;
    } else {
        FreeListNode* prev = list->head;
        while (prev->next != node) {
            prev = prev->next;
        }
        prev->next = node->next;
    }
    free(node);
}

// 根据分块大小选择空闲链表
FreeListNode* selectFreeList(FreeList* list, size_t size) {
    FreeListNode* node = list->head;
    while (node != NULL) {
        if (node->size >= size) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

/**
 * @brief 多个空闲链表
 *  1. 具体就是利用分块大小不同的空闲链表，即创建只连接大分块的空闲链表和只连接小分块的空闲链表
 *  2. 按照 mutator 所申请的分块大小选择空闲链表，就能在短时间内找到符合条件的分块了
 * 
 * @return int 
 */
int main() {
    FreeList smallBlockList;
    FreeList largeBlockList;
    
    initFreeList(&smallBlockList);
    initFreeList(&largeBlockList);
    
    // 向空闲链表中插入节点
    insertFreeListNode(&smallBlockList, 16);
    insertFreeListNode(&smallBlockList, 32);
    insertFreeListNode(&largeBlockList, 64);
    insertFreeListNode(&largeBlockList, 128);
    
    // 根据分块大小选择空闲链表
    size_t blockSize = 20; // 假设需要申请的分块大小为20
    FreeListNode* selectedNode = selectFreeList(&smallBlockList, blockSize);
    if (selectedNode == NULL) {
        selectedNode = selectFreeList(&largeBlockList, blockSize);
    }
    
    if (selectedNode != NULL) {
        printf("找到了合适大小的分块：%zu\n", selectedNode->size);
        // 在这里可以继续处理分块
        removeFreeListNode(&smallBlockList, selectedNode); // 从链表中删除节点
    } else {
        printf("没有找到合适大小的分块\n");
    }
    
    return 0;
}