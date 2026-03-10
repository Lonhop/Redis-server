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
}

#endif //REDIS_SERVER_ZSET_H