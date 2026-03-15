#include "data_structures/avl_tree.h"
#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#include <mutex>

namespace redis::data_structures {

    AVLTree::AVLTree() : comparator_([](const AVLNode* a, const AVLNode* b) { return a < b; }) {}
    void AVLTree::update(AVLNode* node) noexcept {
        if (!node) return;
        node->cnt = 1 + (node->left ? node->left->cnt : 0) + (node->right ? node->right->cnt : 0);
        node->height = 1 + std::max(height(node->left), height(node->right));
    }
    AVLNode* AVLTree::rotate_left(AVLNode* node) noexcept {
        AVLNode* new_root = node->right;
        node->right = new_root->left;
        if (new_root->left) new_root->left->parent = node;
        new_root->parent = node->parent;
        new_root->left = node;
        node->parent = new_root;
        update(node);
        update(new_root);
        return new_root;
    }
    AVLNode* AVLTree::rotate_right(AVLNode* node) noexcept {
        AVLNode* new_root = node->left;
        node->left = new_root->right;
        if (new_root->right) new_root->right->parent = node;
        new_root->parent = node->parent;
        new_root->right = node;
        node->parent = new_root;
        update(node);
        update(new_root);
        return new_root;
    }
    AVLNode* AVLTree::fix(AVLNode* node) noexcept {
        update(node);
        int32_t balance = height(node->left) - height(node->right);
        if (balance > 1) {
            if (height(node->left->right) > height(node->left->left)) {
                node->left = rotate_left(node->left);
            }
            return rotate_right(node);
        }
        else if (balance < -1) {
            if (height(node->right->left) > height(node->right->right)) {
                node->right = rotate_right(node->right);
            }
            return rotate_left(node);
        }
        return node;
    }
    void AVLTree::insert_unsafe(AVLNode* node) {
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
            cur = comparator_(node, cur) ? cur->left : cur->right;
        }
        node->parent = parent;
        if (comparator_(node, parent)) parent->left = node;
        else parent->right = node;
        node->in_tree_flag = true;
        for (cur = node; cur; ) {
            AVLNode* next = cur->parent;
            AVLNode* balanced = fix(cur);
            if (balanced->parent) {
                if (balanced->parent->left == cur) balanced->parent->left = balanced;
                else balanced->parent->right = balanced;
            }
            else {
                root_ = balanced;
            }
            cur = next;
        }
    }
    void AVLTree::insert(AVLNode* node) {
        if (!node) throw std::invalid_argument("nullptr insert");
        std::unique_lock lock(mutex_);
        if (node->in_tree()) throw std::runtime_error("Already in tree");
        insert_unsafe(node);
    }
    AVLNode* AVLTree::remove(AVLNode* node) {
        if (!node) throw std::invalid_argument("nullptr remove");
        std::unique_lock lock(mutex_);
        if (!node->in_tree()) throw std::runtime_error("Not in tree");
        AVLNode* victim = node;
        AVLNode* rebalance_start = nullptr;
        if (node->left && node->right) {
            victim = node->right;
            while (victim->left) victim = victim->left;
            rebalance_start = victim->parent == node ? victim : victim->parent;
            if (victim->parent->left == victim) victim->parent->left = victim->right;
            else victim->parent->right = victim->right;
            if (victim->right) victim->right->parent = victim->parent;
            victim->left = node->left;
            victim->right = node->right;
            victim->parent = node->parent;
            if (node->left) node->left->parent = victim;
            if (node->right) node->right->parent = victim;
            if (node->parent) {
                if (node->parent->left == node) node->parent->left = victim;
                else node->parent->right = victim;
            }
            else {
                root_ = victim;
            }
        }
        else {
            rebalance_start = node->parent;
            AVLNode* child = node->left ? node->left : node->right;
            if (child) child->parent = node->parent;
            if (node->parent) {
                if (node->parent->left == node) node->parent->left = child;
                else node->parent->right = child;
            }
            else {
                root_ = child;
            }
        }
        for (AVLNode* cur = rebalance_start; cur; ) {
            AVLNode* next = cur->parent;
            AVLNode* balanced = fix(cur);
            if (balanced->parent) {
                if (balanced->parent->left == cur) balanced->parent->left = balanced;
                else balanced->parent->right = balanced;
            }
            else {
                root_ = balanced;
            }
            cur = next;
        }
        node->init();
        return node;
    }
    size_t AVLTree::rank(AVLNode* node) const {
        if (!node) return 0;
        std::shared_lock lock(mutex_);
        size_t r = (node->left ? node->left->cnt : 0);
        AVLNode* cur = node;
        while (cur->parent) {
            if (cur == cur->parent->right) {
                r += (cur->parent->left ? cur->parent->left->cnt : 0) + 1;
            }
            cur = cur->parent;
        }
        return r;
    }
    AVLNode* AVLTree::offset(AVLNode* node, int64_t offset) const {
        if (!node) return nullptr;
        std::shared_lock lock(mutex_);
        int64_t target_rank = static_cast<int64_t>(rank(node)) + offset;
        if (target_rank < 0 || (root_ && target_rank >= static_cast<int64_t>(root_->cnt))) return nullptr;
        AVLNode* cur = root_;
        while (cur) {
            int64_t cur_rank = cur->left ? static_cast<int64_t>(cur->left->cnt) : 0;
            if (target_rank < cur_rank) {
                cur = cur->left;
            }
            else if (target_rank > cur_rank) {
                target_rank -= (cur_rank + 1);
                cur = cur->right;
            }
            else {
                return cur;
            }
        }
        return nullptr;
    }
    size_t AVLTree::count(AVLNode* min, AVLNode* max) const {
        if (!min || !max) return 0;
        std::shared_lock lock(mutex_);
        size_t r1 = rank(min);
        size_t r2 = rank(max);
        return (r2 >= r1) ? (r2 - r1 + 1) : 0;
    }
    AVLTree::AVLTree(AVLTree&& other) noexcept : root_(other.root_), comparator_(std::move(other.comparator_)) {
        other.root_ = nullptr;
    }
    AVLTree& AVLTree::operator=(AVLTree&& other) noexcept {
        if (this != &other) { root_ = other.root_; other.root_ = nullptr; }
        return *this;
    }
    void AVLTree::check_invariants() const {
        std::shared_lock lock(mutex_);
        if (!root_) return;
        std::function<int32_t(AVLNode*)> verify = [&](AVLNode* node) -> int32_t {
            if (!node) return 0;
            if (node->left) {
                if (node->left->parent != node)
                    throw std::runtime_error("Invariant failed: left child parent link");
            }
            if (node->right) {
                if (node->right->parent != node)
                    throw std::runtime_error("Invariant failed: right child parent link");
            }
            int32_t left_h = verify(node->left);
            int32_t right_h = verify(node->right);
            if (node->height != 1 + std::max(left_h, right_h))
                throw std::runtime_error("Invariant failed: incorrect height cached in node");
            if (std::abs(left_h - right_h) > 1)
                throw std::runtime_error("Invariant failed: tree out of balance");
            size_t expected_cnt = 1 + (node->left ? node->left->cnt : 0) + (node->right ? node->right->cnt : 0);
            if (node->cnt != expected_cnt)
                throw std::runtime_error("Invariant failed: incorrect node count (cnt)");
            return node->height;
        };
        verify(root_);
    }
}