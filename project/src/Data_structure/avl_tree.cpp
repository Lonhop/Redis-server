#include "data_structures/avl_tree.h"
#include <algorithm>
#include <stdexcept>


namespace redis::data_structures {

    // Фукнции вспомогательные
    static size_t max_size(size_t a, size_t b) noexcept {
        return a > b ? a : b;
    }
    static int32_t get_height(const AVLNode* node) noexcept {
        if (!node) return 0;

        int32_t left_h = node -> left ? 1 + get_height(node->left) : 0;
        int32_t right_h = node -> right ? 1 + get_height(node->right) :0;
        return 1 + max_size(left_h, right_h);
    }

    // Обновление данных узла
    void AVLTree::update(AVLNode* node) noexcept {
        if (!node) return;
        // Обновление размера поддерева
        node->cnt = 1;
        if (node->left) node->cnt += node->left->cnt;
        if (node->right) node->cnt += node->right->cnt;
        // Обновляем балансировку
        int32_t left_h = get_height(node->left);
        int32_t right_h = get_height(node->right);

        if (left_h > right_h) {
            node->balance = Balance::LEFT_HEAVY;
        }
        else if (right_h > left_h) {
            node->balance = Balance::RIGHT_HEAVY;
        }
        else {
            node->balance = Balance::BALANCED;
        }
    }

    // Вращения дерева влево
    AVLNode* AVLTree::rotate_left(AVLNode* node) noexcept {
        if (!node || !node->right) return node;

        AVLNode* new_root = node->right;
        AVLNode* temp = new_root->left;
        // Вращение
        new_root->left = node;
        node->right = temp;
        // Обновляем родителей
        new_root->parent = node->parent;
        node->parent = new_root;
        if (temp) {
            temp->parent = node;
        }
        // Обновляем данные
        update(node);
        updaate(new_root);

        return new_root;
    }
    // Вращение дерева вправо
    AVLNode* AVLTree::rotate_left(AVLNode* node) noexcept {
        if (!node || !node->left) return node;

        AVLNode* new_root = node->left;
        AVLNode* temp = new_root->right;
        // Вращение
        new_root->right = node;
        node->left = temp;
        // Обновляем родителей
        new_root->parent = node->parent;
        node->parent = new_root;
        if (temp) {
            temp->parent = node;
        }
        // Обновляем данные
        update(node);
        updaate(new_root);

        return new_root;
    }
}