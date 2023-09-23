#include <stdio.h>
#include <stdlib.h>

// 定义二叉树节点
typedef struct TreeNode {
    int size;               // 块的大小
    int allocated;          // 是否已分配
    struct TreeNode* left;  // 左子节点
    struct TreeNode* right; // 右子节点 
} TreeNode;

// 初始化二叉树节点
void initTreeNode(TreeNode* node, int size) {
    node->size = size;
    node->allocated = 0;
    node->left = NULL;
    node->right = NULL;
}

// 分配内存
void* allocateMemory(TreeNode* node, int size) {
    if (node->size < size || node->allocated) {
        return NULL;    // 没有足够的内存块可用
    }

    if (node->size == size) {
        node->allocated = 1;    // 找到合适的内存块，标记为已分配
        return node;
    }

    if (node->left == NULL) {
        node->left = malloc(sizeof(TreeNode));
        initTreeNode(node->left, node->size / 2);
        node->right = malloc(sizeof(TreeNode));
        initTreeNode(node->right, node->size / 2);
    }

    void* result = allocateMemory(node->left, size);
    if (result == NULL) {
        result = allocateMemory(node->right, size);
    }

    return result;
}

// 释放内存
void freeMemory(TreeNode* node, void* memory) {
    if (node->left == NULL && node->right == NULL) {
        if (node == memory) {
            node->allocated = 0;    // 标记为未分配
        }
        return;
    }

    freeMemory(node->left, memory);
    freeMemory(node->right, memory);

    // 尝试与相邻的未分配块进行合并
    if (!node->left->allocated && !node->right->allocated) {
        node->left = NULL;
        node->right = NULL;
    }
}

/**
 * @brief BIBOP算法
 *  BIBOP算法的主要思想是将内存划分为大小相等的块，并使用二叉树来管理这些块
 *  每个块的大小是2的幂次方，例如，大小为2^k的块被划分为k级。初始时，只有一块大小为2^N的内存可用，其中N是整个堆的大小。
 * @return int 
 */
int main() {
  TreeNode root;
  initTreeNode(&root, 1024);    // 初始化根节点，大小为1024
  
  // 分配内存
  void* memory1 = allocateMemory(&root, 128);
  void* memory2 = allocateMemory(&root, 256);
  
  // 释放内存
  freeMemory(&root, memory1);
  freeMemory(&root, memory2);
  
  return 0;
}