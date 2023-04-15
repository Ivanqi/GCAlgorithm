#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// 内存块结构体定义
typedef struct block {
    struct block* next;
    struct block* prev;
    bool marked; // 是否标记为活跃内存
} Block;

// 内存块大小
#define BLOCK_SIZE sizeof(Block)

// 内存块链表
Block* head = NULL;

// 分配内存块
Block* newBlock() {
    Block* block = (Block *) malloc(BLOCK_SIZE);
    if (block == NULL) {
        printf("Failed to allocate memory!\n");
        exit(1);
    }

    block->next = NULL;
    block->prev = NULL;
    block->marked = false;
    return block;
}

//  添加内存块到链表头部
void addToHeap(Block* block) {
    block->next = head;
    block->prev = NULL;
    if (head != NULL) {
        head->prev = block;
    }
    head = block;
}

// 标记活跃内存块
void mark(Block* block) {
    if (block == NULL || block->marked) return;
    block->marked = true;
}

// 清除非活跃内存块
void sweep() {
    Block* block = head;
    while (block != NULL) {
        if (!block->marked) {
            // 如果该内存块未标记，即非活跃，将其从链表中移除并释放内存
            if (block->prev != NULL) {
                block->prev->next = block->next;
            }

            if (block->next != NULL) {
                block->next->prev = block->prev;
            }

            if (block == head) {
                head = block->next;
            }

            Block* tmp = block;
            block = block->next;
            free(tmp);
        } else {
            // 如果该内存块已标记，即活跃，将标记清除
            block->marked = false;
            block = block->next;   
        }
    }
}

// 垃圾回收函数
void collect() {
    // 标记活跃内存块
    Block* block = head;
    while (block != NULL) {
        if (block->marked) {
            // 如果该内存块已标记，即活跃，递归扫描其内部以标记其他活跃块
            if (block->next != NULL) {
                mark((Block*)block->next);
            }
            
        }
        block = block->next;
    }

    // 清除非活跃内存块
    sweep();
}

// 测试案例
int main() {

    // 分配一些内存块
    Block* a = newBlock();
    Block* b = newBlock();
    Block* c = newBlock();

    // 将内存块添加到链表
    addToHeap(a);
    addToHeap(b);
    addToHeap(c);

    // 标记第1个和第3个内存为活跃
    mark(a);
    mark(c);

    // 进行垃圾回收
    collect();

    // 输出剩余内存块
    Block* block = head;
    while (block != NULL) {
        printf("Block Address: %p, Marked: %d\n", block, block->marked);
        block = block->next;
    }

    // 释放内存块
    free(a);
    free(b);
    free(c);
    
    return 0;
}