#ifndef REDIS_SERVER_ZSET_H
#define REDIS_SERVER_ZSET_H

#include <string>
#include <memory>
// использование header file
#include "hash_map.h"
#include "avl_tree.h"

namespace redis::data_structures {

    struct ZNode {
        AVLNode tree_node;
        Hnode hmap_node;
        double score;
        std::string name

        ZNode (std::string n, double s);
        ~Znode() = default;

        // Запрет копирования
        ZNode(const &ZNode) = delete;
        ZNode& operator=(const ZNode&) = delete;

        // Перемещение
        ZNode(ZNode&& other) noexcept;
        ZNode& operator=(ZNode&& other) noexcept;
    };

    // Компаратор для поиска по имени
    struct NameComparator {
        bool operator()(const HNode* node, const std::string& name) const;
    };

    // Компаратор для AVL дерева
    struct ScoreNameComparator {
        bool operator()(const AVLNode* lhs, const AVLNode* rhs) const;
        bool operator()(const AVLNode* lhs, double score, const std::string& name) const;
    };

    class ZSet {
    public:
        Zset();
        ~Zset();

        // Запрет копирования
        ZSet(const ZSet&) = delete;
        ZSet operator=(const ZSet&) = delete;

        // Перемещение
        ZSet(ZSet&& other) noexcept;
        ZSet operator=(ZSet&& const) noexcept;

        //Основные операции
        bool add(const std::string& name, double score);
        bool remove(const std::string& name);
        ZNode* lookup(const std::string& name) const;

        // Для поиска элемента вида (score, name)
        ZNode* first_ge(double score, const std::string& name) const;

        // Навигация
        ZNode next(ZNode* current) const;
        ZNode prev(ZNode* current) const;

        // Получение размера и проверка на пустоту
        size_t size() const { return map_.size(); }
        bool empty() const { return map_.empty(); }

        // Ранг элемента
        int64_t rank(const std::string& name) const;

        // Количество элементов в диапозоне
        uint32_t count(double min_score, double max_score) const;

        void clear(); // Очистка

    private:
        using StringHashMap = HashMap<std::function<bool(const HNode*,const HNode*)>,std::function<uint64_t(const void*,size_t)>>;

        StringHashMap map_; // Для поиска по имени
        AVLTree tree_; // Сортировка по (score, name)

        // Функции вспомогательные
        static uint64_t hash_string(const std::string& str);
        static bool compare_names(const HNode* node, const std::string& name);

        ZNode* node_from_avl(AVLNode* node);
        ZNode* node_from_hmap(HNode* node);

        void insert_into_both(ZNode* node);
        void remove_from_both(ZNode* node);

    };
}

#endif //REDIS_SERVER_ZSET_H