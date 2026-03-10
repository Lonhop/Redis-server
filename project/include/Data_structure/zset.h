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
}

#endif //REDIS_SERVER_ZSET_H