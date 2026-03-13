#ifndef REDIS_SERVER_AVL_TREE_H
#define REDIS_SERVER_AVL_TREE_H

#include <cstdint>
#include <stdexcept>
#include <shared_mutex>
#include <concepts>
#include <functional>

namespace redis::data_structures {
    // Балансировка узла AVL дерева
    enum class Balance : int32_t {
        LEFT_HEAVY = -1,   // левое поддерево выше на 1
        BALANCED = 0,       // высоты равны
        RIGHT_HEAVY = 1     // правое поддерево выше на 1
    };

    // Узел AVL дерева
    struct AVLNode {
        AVLNode* left = nullptr;
        AVLNode* right = nullptr;
        AVLNode* parent = nullptr;
        Balance balance = Balance::BALANCED;
        size_t cnt = 1;      // размер поддерева
        bool in_tree_flag = false;  // явный флаг нахождения в дереве

        // Инициализация нового узла
        void init() noexcept {
            left = right = parent = nullptr;
            balance = Balance::BALANCED;
            cnt = 1;
            in_tree_flag = false;
        }

        // Проверка находится ли узел в дереве
        bool in_tree() const noexcept {
            return in_tree_flag;
        }
    };

    template<typename C, typename T>
    concept Comparator = requires(C cmp, const T* a, const T* b) {
        { cmp(a, b) } -> std::convertible_to<bool>;
    };

    class AVLTree {
    public:
        // Конструктор по умолчанию (использует сравнение указателей)
        AVLTree();

        // Конструктор с пользовательским компаратором
        template<typename Compare>
        explicit AVLTree(Compare cmp) : comparator_(cmp) {}

        // Запрет копирования
        AVLTree(const AVLTree&) = delete;
        AVLTree& operator=(const AVLTree&) = delete;

        // Перемещение
        AVLTree(AVLTree&& other) noexcept;
        AVLTree& operator=(AVLTree&& other) noexcept;

        // Деструктор
        ~AVLTree() = default;

        // Вставка узла в дерево
        void insert(AVLNode* node);

        // Удаление узла из дерева
        AVLNode* remove(AVLNode* node);

        // Поиск узла по ключу
        template<typename K, typename Compare>
        AVLNode* find(const K& key, Compare cmp) const {
            std::shared_lock lock(mutex_);
            AVLNode* cur = root_;
            while (cur) {
                if (cmp(key, cur))
                    cur = cur->left;
                else if (cmp(cur, key))
                    cur = cur->right;
                else
                    return cur;
            }
            return nullptr;
        }

        // Получить корень дерева
        AVLNode* root() const {
            std::shared_lock lock(mutex_);
            return root_;
        }

        // Получить узел со смещением относительно данного
        AVLNode* offset(AVLNode* node, int64_t offset) const;

        // Ранг узла
        size_t rank(AVLNode* node) const;

        // Количество узлов
        size_t size() const {
            std::shared_lock lock(mutex_);
            return root_ ? root_->cnt : 0;
        }

        bool empty() const {
            std::shared_lock lock(mutex_);
            return root_ == nullptr;
        }

        // Количество узлов в диапазоне
        size_t count(AVLNode* min, AVLNode* max) const;

    #ifndef NDEBUG
        void check_invariants() const;
    #endif

    private:
        mutable std::shared_mutex mutex_;
        AVLNode* root_ = nullptr;
        std::function<bool(const AVLNode*, const AVLNode*)> comparator_;

        static void update(AVLNode* node) noexcept;
        static AVLNode* rotate_left(AVLNode* node) noexcept;
        static AVLNode* rotate_right(AVLNode* node) noexcept;
        static AVLNode* fix(AVLNode* node) noexcept;
        static AVLNode* fix_left(AVLNode* node) noexcept;
        static AVLNode* fix_right(AVLNode* node) noexcept;
        static AVLNode* detach_min(AVLNode** pnode) noexcept;

        void insert_unsafe(AVLNode* node);
        AVLNode* remove_unsafe(AVLNode* node);
    };

}

#endif