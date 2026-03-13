#ifndef REDIS_SERVER_AVL_TREE_H
#define REDIS_SERVER_AVL_TREE_H

#include <cstdint>
#include <stdexcept>
#include <shared_mutex>
#include <concepts>
#include <functional>
#include <algorithm>

namespace redis::data_structures {

    struct AVLNode {
        AVLNode* left = nullptr;
        AVLNode* right = nullptr;
        AVLNode* parent = nullptr;
        int32_t height = 1;  // Высота для O(1) доступа
        size_t cnt = 1;      // Размер поддерева для rank/offset
        bool in_tree_flag = false;

        void init() noexcept {
            left = right = parent = nullptr;
            height = 1;
            cnt = 1;
            in_tree_flag = false;
        }

        bool in_tree() const noexcept {
            return in_tree_flag;
        }
    };

    class AVLTree {
    public:
        AVLTree();

        template<typename Compare>
        explicit AVLTree(Compare cmp) : comparator_(cmp) {}

        AVLTree(const AVLTree&) = delete;
        AVLTree& operator=(const AVLTree&) = delete;

        AVLTree(AVLTree&& other) noexcept;
        AVLTree& operator=(AVLTree&& other) noexcept;

        ~AVLTree() = default;

        void insert(AVLNode* node);
        AVLNode* remove(AVLNode* node);

        template<typename K, typename Compare>
        AVLNode* find(const K& key, Compare cmp) const {
            std::shared_lock lock(mutex_);
            AVLNode* cur = root_;
            while (cur) {
                if (cmp(key, cur)) cur = cur->left;
                else if (cmp(cur, key)) cur = cur->right;
                else return cur;
            }
            return nullptr;
        }

        AVLNode* root() const { std::shared_lock lock(mutex_); return root_; }
        AVLNode* offset(AVLNode* node, int64_t offset) const;
        size_t rank(AVLNode* node) const;
        size_t size() const { std::shared_lock lock(mutex_); return root_ ? root_->cnt : 0; }
        bool empty() const { std::shared_lock lock(mutex_); return root_ == nullptr; }
        size_t count(AVLNode* min, AVLNode* max) const;

#ifndef NDEBUG
        void check_invariants() const;
#endif

    private:
        mutable std::shared_mutex mutex_;
        AVLNode* root_ = nullptr;
        std::function<bool(const AVLNode*, const AVLNode*)> comparator_;

        static int32_t height(AVLNode* node) noexcept { return node ? node->height : 0; }
        static void update(AVLNode* node) noexcept;
        static AVLNode* rotate_left(AVLNode* node) noexcept;
        static AVLNode* rotate_right(AVLNode* node) noexcept;
        static AVLNode* fix(AVLNode* node) noexcept;

        void insert_unsafe(AVLNode* node);
        void remove_node_ptr(AVLNode* node); // Вспомогательная для удаления
    };
}

#endif