#include "data_structures/avl_tree.h"
#include <algorithm>
#include <stdexcept>
#include <cstdlib>

namespace redis::data_structures {
    static size_t max_size(size_t a, size_t b) noexcept {
        return a > b ? a : b;
    }

    static int32_t get_height(const AVLNode* node) noexcept {
        if (!node) return 0;
        int32_t left_h = node->left ? 1 + get_height(node->left) : 0;
        int32_t right_h = node->right ? 1 + get_height(node->right) : 0;
        return 1 + max_size(left_h, right_h);
    }

    // Конструктор по умолчанию
    AVLTree::AVLTree()
        : comparator_([](const AVLNode* a, const AVLNode* b) { return a < b; }) {}

    void AVLTree::update(AVLNode* node) noexcept {
        if (!node) return;
        node->cnt = 1;
        if (node->left) node->cnt += node->left->cnt;
        if (node->right) node->cnt += node->right->cnt;
        int32_t left_h = get_height(node->left);
        int32_t right_h = get_height(node->right);
        if (left_h > right_h) {
            node->balance = Balance::LEFT_HEAVY;
        } else if (right_h > left_h) {
            node->balance = Balance::RIGHT_HEAVY;
        } else {
            node->balance = Balance::BALANCED;
        }
    }

    AVLNode* AVLTree::rotate_left(AVLNode* node) noexcept {
        if (!node || !node->right) return node;
        AVLNode* new_root = node->right;
        AVLNode* temp = new_root->left;
        new_root->left = node;
        node->right = temp;
        new_root->parent = node->parent;
        node->parent = new_root;
        if (temp) {
            temp->parent = node;
        }
        update(node);
        update(new_root);
        return new_root;
    }

    AVLNode* AVLTree::rotate_right(AVLNode* node) noexcept {
        if (!node || !node->left) return node;
        AVLNode* new_root = node->left;
        AVLNode* temp = new_root->right;
        new_root->right = node;
        node->left = temp;
        new_root->parent = node->parent;
        node->parent = new_root;
        if (temp) {
            temp->parent = node;
        }
        update(node);
        update(new_root);
        return new_root;
    }

    AVLNode* AVLTree::fix_left(AVLNode* node) noexcept {
        if (!node || !node->left) return node;
        if (node->left->right && get_height(node->left->right) > get_height(node->left->left)) {
            node->left = rotate_left(node->left);
        }
        return rotate_right(node);
    }

    AVLNode* AVLTree::fix_right(AVLNode* node) noexcept {
        if (!node || !node->right) return node;
        if (node->right->left && get_height(node->right->left) > get_height(node->right->right)) {
            node->right = rotate_right(node->right);
        }
        return rotate_left(node);
    }

    AVLNode* AVLTree::fix(AVLNode* node) noexcept {
        if (!node) return nullptr;
        int32_t left_h = get_height(node->left);
        int32_t right_h = get_height(node->right);
        if (left_h > right_h + 1) {
            return fix_left(node);
        } else if (right_h > left_h + 1) {
            return fix_right(node);
        }
        return node;
    }

    AVLNode* AVLTree::detach_min(AVLNode** pnode) noexcept {
        if (!pnode || !*pnode) return nullptr;
        AVLNode* node = *pnode;
        while (node->left) {
            pnode = &node->left;
            node = node->left;
        }
        *pnode = node->right;
        if (node->right) {
            node->right->parent = node->parent;
        }
        node->left = node->right = nullptr;
        node->parent = nullptr;
        update(node);
        return node;
    }

    void AVLTree::insert_unsafe(AVLNode* node) {
        if (!node) return;
        node->init();

        if (!root_) {
            root_ = node;
            node->in_tree_flag = true;
            return;
        }

        AVLNode* cur = root_;
        AVLNode* parent = nullptr;

        while (cur) {
            parent = cur;
            if (comparator_(node, cur)) {
                cur = cur->left;
            } else {
                cur = cur->right;
            }
        }

        node->parent = parent;
        if (comparator_(node, parent)) {
            parent->left = node;
        } else {
            parent->right = node;
        }

        node->in_tree_flag = true;

        cur = node;
        while (cur) {
            update(cur);
            AVLNode* balanced = fix(cur);
            if (balanced != cur) {
                if (balanced->parent) {
                    if (balanced->parent->left == cur) {
                        balanced->parent->left = balanced;
                    } else {
                        balanced->parent->right = balanced;
                    }
                } else {
                    root_ = balanced;
                }
                balanced->in_tree_flag = true;
                cur = balanced;
            } else {
                cur = cur->parent;
            }
        }
    }

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

    AVLNode* AVLTree::remove_unsafe(AVLNode* node) {
        if (!node) return nullptr;
        AVLNode* parent = node->parent;
        AVLNode* new_root = root_;

        node->in_tree_flag = false;

        if (!node->left && !node->right) {
            if (parent) {
                if (parent->left == node) parent->left = nullptr;
                else parent->right = nullptr;
                new_root = fix(parent);
            } else {
                new_root = nullptr;
            }
        } else if (node->left && !node->right) {
            if (parent) {
                if (parent->left == node) parent->left = node->left;
                else parent->right = node->left;
                node->left->parent = parent;
                new_root = fix(parent);
            } else {
                node->left->parent = nullptr;
                new_root = node->left;
                fix(new_root);
            }
        } else if (!node->left && node->right) {
            if (parent) {
                if (parent->left == node) parent->left = node->right;
                else parent->right = node->right;
                node->right->parent = parent;
                new_root = fix(parent);
            } else {
                node->right->parent = nullptr;
                new_root = node->right;
                fix(new_root);
            }
        } else {
            AVLNode* succ = node->right;
            while (succ->left) succ = succ->left;
            AVLNode* succ_parent = succ->parent;
            bool isLeftChild = (succ_parent && succ_parent->left == succ);

            if (succ_parent) {
                if (isLeftChild) succ_parent->left = succ->right;
                else succ_parent->right = succ->right;
            }
            if (succ->right) succ->right->parent = succ_parent;

            succ->left = node->left;
            succ->right = node->right;
            succ->parent = node->parent;

            if (node->left) node->left->parent = succ;
            if (node->right) node->right->parent = succ;
            update(succ);

            if (parent) {
                if (parent->left == node) parent->left = succ;
                else parent->right = succ;
            } else {
                root_ = succ;
            }

            succ->in_tree_flag = true;

            AVLNode* start_balance = succ_parent;
            if (start_balance == node) start_balance = succ;

            while (start_balance) {
                AVLNode* next = start_balance->parent;
                AVLNode* balanced = fix(start_balance);
                if (balanced != start_balance) {
                    if (balanced->parent) {
                        if (balanced->parent->left == start_balance) {
                            balanced->parent->left = balanced;
                        } else {
                            balanced->parent->right = balanced;
                        }
                    } else {
                        root_ = balanced;
                    }
                    balanced->in_tree_flag = true;
                }
                start_balance = next;
            }
            new_root = root_;
        }

        node->left = node->right = node->parent = nullptr;
        node->cnt = 1;
        node->balance = Balance::BALANCED;
        return new_root;
    }

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

    AVLNode* AVLTree::offset(AVLNode* node, int64_t offset) const {
        if (!node) return nullptr;
        std::shared_lock lock(mutex_);
        AVLNode* cur = node;
        int64_t remaining = offset;

        while (cur && remaining != 0) {
            if (remaining > 0) {
                if (cur->right) {
                    size_t left_size = cur->right->left ? cur->right->left->cnt : 0;
                    if (remaining <= static_cast<int64_t>(left_size)) {
                        cur = cur->right->left;
                        continue;
                    }
                    remaining -= left_size + 1;
                    if (remaining == 0) return cur->right;
                    cur = cur->right;
                } else {
                    while (cur->parent && cur == cur->parent->right) {
                        cur = cur->parent;
                    }
                    if (cur->parent) {
                        remaining--;
                        if (remaining == 0) return cur->parent;
                        cur = cur->parent;
                    } else {
                        return nullptr;
                    }
                }
            } else {
                if (cur->left) {
                    size_t right_size = cur->left->right ? cur->left->right->cnt : 0;
                    if (-remaining <= static_cast<int64_t>(right_size)) {
                        cur = cur->left->right;
                        remaining++;
                        continue;
                    }
                    remaining += right_size + 1;
                    if (remaining == 0) return cur->left;
                    cur = cur->left;
                } else {
                    while (cur->parent && cur == cur->parent->left) {
                        cur = cur->parent;
                    }
                    if (cur->parent) {
                        remaining++;
                        if (remaining == 0) return cur->parent;
                        cur = cur->parent;
                    } else {
                        return nullptr;
                    }
                }
            }
        }
        return cur;
    }

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

    size_t AVLTree::count(AVLNode* min, AVLNode* max) const {
        if (!min || !max) return 0;
        std::shared_lock lock(mutex_);
        size_t min_rank = rank(min);
        size_t max_rank = rank(max);
        if (max_rank < min_rank) return 0;
        return max_rank - min_rank + 1;
    }

#ifndef NDEBUG
    void AVLTree::check_invariants() const {
        std::shared_lock lock(mutex_);

        auto check_node = [&](AVLNode* node, auto&& self) -> int {
            if (!node) return 0;

            if (node->left) {
                if (node->left->parent != node) {
                    throw std::runtime_error("Invalid left parent link");
                }
            }
            if (node->right) {
                if (node->right->parent != node) {
                    throw std::runtime_error("Invalid right parent link");
                }
            }

            int left_h = self(node->left, self);
            int right_h = self(node->right, self);

            if (std::abs(left_h - right_h) > 1) {
                throw std::runtime_error("AVL balance violated");
            }

            size_t expected_cnt = 1;
            if (node->left) expected_cnt += node->left->cnt;
            if (node->right) expected_cnt += node->right->cnt;

            if (node->cnt != expected_cnt) {
                throw std::runtime_error("Invalid subtree count");
            }

            return 1 + max_size(left_h, right_h);
        };

        check_node(root_, check_node);
    }
#endif

    AVLTree::AVLTree(AVLTree&& other) noexcept
        : root_(other.root_)
        , comparator_(std::move(other.comparator_)) {
        other.root_ = nullptr;
    }

    AVLTree& AVLTree::operator=(AVLTree&& other) noexcept {
        if (this != &other) {
            root_ = other.root_;
            comparator_ = std::move(other.comparator_);
            other.root_ = nullptr;
        }
        return *this;
    }

}