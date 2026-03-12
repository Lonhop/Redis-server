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

    // Балансировка слева
    AVLNode* AVLTree::fix_left(AVLNode* node) noexcept {
        if (!node || !node->left) return node;

        // LR(left to right)
        if (node->left->right && get_height(node->left->right) > get_height(node->left->left)) {
            node->left = rotate_left(node->left);
        }
        return rotate_right(node);
    }
    // Балансировка справа
    AVLNode* AVLTree::fix_left(AVLNode* node) noexcept {
        if (!node || !node->right) return node;

        // RL(right to left)
        if (node->right->left && get_height(node->right->left) > get_height(node->right->right)) {
            node->right = rotate_right(node->right);
        }
        return rotate_left(node);
    }
    // Балансировка
    AVLNode* AVLTree::fix(AVLNode* node) noexcept {
        if (!node) return nullptr;

        // Проверка баланса и их исправление в случае проблемы
        int32_t left_h = get_height(node->left);
        int32_t right_h = get_height(node->right);
        if (left_h > right_h + 1) {
            return fix_left(node);
        }
        else if (right_h > left_h +1) {
            return fix_right(node);
        }
        return node;
    }

    // Вставка
    void AVLTree:insert_unsafe(AVLNode* node) {
        if (!node) return;
        node->init(); // сброс состояния узла

        if (!root_) {
            root_ = node;
            return;
        }
        AVLNode* cur = root);
        AVLNode* parent = nullptr;
        while (cur) {
            if (node < cur) {
                cur = cur->left;
            }
            else {
                cur = cur->right;
            }
        }
        // Вставка узла
        node->parent = parent;
        if (node < parent) {
            parent->left = node;
        }
        else {
            parent->right = node;
        }

        // Балансировка дерева
        cur = node;
        while (cur) {
            update(cur);
            AVLNode* next = cur->current;

            if (next) {
                if (next->left == cur) {
                    update(next);
                    AVLNode* balanced = fix(next);
                    if (balanced->parent) {
                        if (balanced->parent->left == next) balanced->parent->left = balanced;
                        else balanced->parent->right = balanced
                    }
                    else {
                        root_ = balanced;
                    }
                }
                else {
                    update(next);
                    AVLNode* balanced = fix(next);
                    if (balanced != next) {
                        if (balanced->parent) {
                            if (balanced->parent->left == next) balanced->parent->left = balanced;
                            else balanced->parent->right = balanced;
                        }
                        else {
                            root_ = balanced;
                        }
                    }
                }
            }
            cur = fix(cur);
            if (!cur->parent) {
                root_ = cur;
                break;
            }
            cur = cur->parent;
        }
    }

    // Вставка
    void AVLTree::insert(AVLNode* node) {
        if (!node) {
            throw std::invalid_argument("AVLTree::insert: can't be nullptr");
        }

        std::unique_lock lock(mutex_);
        if (node->in_tree()) {
            throw std::runtime_error("AVLTree::insert: already in tree");
        }
        insert_unsafe(node);
    }

    // Удаление
    AVLNode* AVLTree::remove_unsafe(AVLNode* node) {
        if (!node) return nullptr;
        AVLNode* parent = node->parent;
        AVLNode* new_root = root_;

        // Ни слева ни справа
        if (!node->left && !node->right) {
            if (parent) {
                if (parent->left == node) parent->left = nullptr;
                else parent->right = nullptr;
                new_root = fix(parent);
            }
            else {
                new_root = nullptr;
            }
        }
        // Только слева
        else if (node->left && !node->right) {
            if (parent) {
                if (parent->left == node) parent->left = node->left;
                else parent->right = node->left;
                node->left->parent = parent;
                new_root = fix(parent);
            }
            else {
                node->left->parent = nullptr;
                new_root = node->left;
                fix(new_root);
            }
        }
        // Только справа
        else if (!node->left && node->right) {
            if (parent) {
                if (parent->left == node) parent->left = node->right;
                else parent->right = node->right;
                node->right->parent = parent;
                new_root = fix(parent);
            }
            else {
                node->right->parent = nullptr;
                new_root = node->right;
                fix(new_root);
            }
        }
        // Слева и справа
        else {
            // Ищем приемника справа
            AVLNode* succ = node->right;
            while (succ->left) succ = succ->left;
            AVLNode* succ_parent = succ->parent;
            bool isLeftChild = (succ_parent && succ_parent->left == succ);

            // Отсоединение приемника
            if (succ_parent) {
                if (isLeftChild) succ_parent->left = succ->right;
                else succ_parent->right = succ->right;
            }
            if (succ->right) succ->right->parent = succ_parent;

            // Перенос детей
            succ->left = node->left;
            succ->right = node->right;
            succ->parent == node->parent;
            if (node->left) node->left->parent = succ;
            if (node->right) node->right->parent = succ;
            update(succ);

            // Подключаем преемника
            if (parent) {
                if (parent->left == node) parent->left = succ;
                else parent->right = succ;
            }
            else {
                root_ = succ;
            }
            // Балансирование родитель -> преемник
            AVlNode* start_balance = succ_parent;
            if (start_balance == node) start_balance = succ;

            while (start_balance) {
                AVLNode* next = start_balance->parent;
                AVLNode* balanced = fix(start_balance);
                if (balanced != start_balance) {
                    if (balanced->parent) {
                        if (balanced->parent->left == start_balance) balanced->parent->left = balanced;
                        else balanced->parent->right = balanced;
                    }
                    else {
                        root_ = balanced;
                    }
                    start_balance = next;
                }
                new_root = root_;
            }
        }
        node->left = node->right = node->parent = nullptr;
        node->cnt = 1;
        node->balance = Balance::BALANCED;
        return new_root;
    }

    // Удаление с проверкой
    AVLNode* AVLTree::remove(AVLNode* node) {
        if (!node) {
            throw std::invalid_argument("AVLTree::remove: can't be nullptr");
        }
        std::unique_lock lock(mutex_);
        if (!node->in_tree()) {
            throw std::runtime_error("AVLTree::remove: not in tree");
        }
        root_ = remove_unsafe(node);
        return node;
    }

    // Перемещение по дереву(отсортированном)
    AVLNode* AVLTree::offset(AVLNode* node, int64_t offset) const {
        if (!node) return nullptr;
        std::shared_lock lock(mutex_);
        AVLNode* cur = node;
        int64_t remaining = offset;
        // Доходим до нужного значения offset
        while (cur && remaing != 0) {
            if (remaining > 0) {
                // Вверх
                (if cur->right) {
                    size_t left_size = cur->right->left ? cur->right->left->cnt : 0;
                    if (remaining <= static_cast<int64_t>(left_size)) {
                        cur = cur->right->left;
                        continue;
                    }
                    remaining -= left_size + 1;
                    if (remaining == 0) return cur->right;
                    cur = cur->right;
                }
                else {
                    while (cur->parent && cur == cur->parent=>right) {
                        cur = cur->parent;
                    }
                    if (cur->parent) {
                        remaining--;
                        if (remaining == 0) reteurn cur->parent;
                        cur = cur->parent;
                    }
                    else {
                        return nullptr;
                    }
                }
            }
            // Вниз
            else {
                if (cur->left) {
                    size_t right_size = cur->left->right ? cur->left->right->cnt : 0;
                    if (-remaining <= static_cast<int64_t>(right_size)) {
                        cur = cur->left->right;
                        remaining++;
                        continue;
                    }
                    remaining += right_size + 1;
                    if (remaining == 0) return cur->left;
                    cur = cur->left
                }
                else {
                    while (cur->parent && cur == cur->parent->left) {
                        cur = cur->parent;
                    }
                    if (cur->parent) {
                        remaining++;
                        if (remaining == 0) return cur->parent;
                        cur = cur->parent;
                    }
                    else return nullptr
                }
            }
        }
        return  cur;
    }
    // Ранг
    size_t AVLTree::rank(AVLNode* node) const {
        if (!node) {
            throw std::invalid_argument("AVLTree::rank: can't be nullptr");
        }
        std::shared_lock lock(mutex_);
        size_t r = node->left ? node->left->cnt : 0;
        AVLNode* cur = node;
        while (cur->parent) {
            if (cur == cur->parent->right) {
                r += (cur->parent->left ? cur->parent->left->cnt : 0) + 1;
            }
            cur = cur->parent;
        }
        return r;
    }
    // Счет в диапозоне
    size_t AVLTree::count(AVLNode* min, AVLNode* max) const {
        if (!min || !max) return 0;
        std::shared_lock lock(mutex_);
        size_t min_rank = rank(min);
        size_t max_rank = rank(max);
        if (max_rank < min_rank) return 0;
        return max_rank - min_rank + 1;
    }

    // Проверка
    #ifndef NDEBUG // дабы не превращать из O(log(n)) -> O(n)
        void AVLTree::chack_invariants() const {
            std::shared_lock lock(mutex_);
            auto ckeck_node = [&](AVLNode* node, auto&& self) -> int {
                if (!node) return 0;

                if (node->left) {
                    if (node->left->parent != node) {
                        throw std::runtime_error("Invalid link");
                    }
                }
                if (node->right) {
                    if (node->right->parent != node) {
                        throw std::runtime_error("Invalid link");
                    }
                }
                int left_h = self(node->left,self);
                int right_h = self(node->right,self);
                if (std::abs(left_h - right_h) > 1) {
                    throw std::runtime_error("AVL balance isn't balanced");
                }
                size_t expected_cnt = 1;
                if (node->left) expected_cnt += node->left->cnt;
                if (node->right) expected_cnt += node->right->cnt;
                if (node->cnt != expected_cnt) {
                    throw std::runtime_error("Invalid count");
                }
                return 1 + max_size(left_h, right_h);
            };
            check_node(root_, check_node, nullptr, nullptr);
    }
    #endif

}