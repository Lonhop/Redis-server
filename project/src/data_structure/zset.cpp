#include "data_structures/zset.h"
#include <cstring>
#include <limits>

namespace redis::data_structures {

    // Конструкторы(для структуры)
    ZNode::ZNode(std::string n, double s) : score(s), name(std::move(n)) {
        tree_node.init();
        hmap_node.next = nullptr;
        hmap_node.hcode = ZSet::hash_string(name);
    }
    ZNode::ZNode(ZNode&& other) noexcept : tree_node(std::move(other.tree_node)), hmap_node(std::move(other.hmap_node)), score(other.score), name(std::move(other.name)) {
        // Обновляем
        if (tree_node.left) tree_node.left->parent = &tree_node;
        if (tree_node.right) tree_node.right->parent = &tree_node;
    }
    ZNode& ZNode::operator=(ZNode&& other) noexcept {
        if (this != &other) {
            tree_node = std::move(other.tree_node);
            hmap_node = std::move(other.hmap_node);
            score = other.score;
            name = std::move(other.name);
            // Обновляем
            if (tree_node.left) tree_node.left->parent = &tree_node;
            if (tree_node.right) tree_node.right->parent = &tree_node;
        }
        return *this;
    }

    // operator()
    bool NameComparator::operator()(const HNode* node, const std::string& name) const {
        auto* zn = container_of(node, ZNode, hmap_node);
        return zn->name == name;
    }
    bool ScoreNameComparator::operator()(const AVLNode* lhs, const AVLNode* rhs) const {
        auto* lz = container_of(lhs, ZNode, tree_node);
        auto* rz = container_of(rhs, ZNode, tree_node);
        if (lz->score != rz->score) {
            return lz->score < rz->score;
        }
        return lz->name < rz->name;
    }
    bool ScoreNameComparator::operator()(const AVLNode* lhs, double score, const std::string& name) const {
        auto* lz = container_of(lhs, ZNode, tree_node);
        if (lz->score != score) {
            return lz->score < score;
        }
        return lz->name < name;
    }


    // Конструкторы(класса)
    ZSet::ZSet() : map_(StringHashMap::default_hash,[](const HNode* a, const HNode* b) { return a == b; }), tree_(ScoreNameComparator()) {
    }
    ZSet::ZSet(ZSet&& other) noexcept: map_(std::move(other.map_)), tree_(std::move(other.tree_)) {
    }
    ZSet& ZSet::operator=(ZSet&& other) noexcept {
        if (this != &other) {
            clear();
            map_ = std::move(other.map_);
            tree_ = std::move(other.tree_);
        }
        return *this;
    }
    // Деструктор
    ZSet::~ZSet() {
        clear();
    }

    // Вспомогательные фукнции
    uint64_t ZSet::hash_string(const std::string& str) {
        return StringHashMap::default_hash(str.data(), str.size());
    }
    bool ZSet::compare_names(const HNode* node, const std::string& name) {
        auto* zn = container_of(node, ZNode, hmap_node);
        return zn->name == name;
    }
    ZNode* ZSet::node_from_avl(AVLNode* node) {
        return node ? container_of(node, ZNode, tree_node) : nullptr;
    }
    ZNode* ZSet::node_from_hmap(HNode* node) {
        return node ? container_of(node, ZNode, hmap_node) : nullptr;
    }
    void ZSet::insert_into_both(ZNode* node) {
        map_.insert(&node->hmap_node); // Вставка в хеш таблицу
        tree_.insert(&node->tree_node); // Вставка в AVL дерево
    }
    void ZSet::remove_from_both(ZNode* node) {
        HNode* removed = map_.erase(&node->hmap_node); // Удаление из хеш-таблицы
        if (removed) {
            tree_.remove(&node->tree_node); // Удаление из AVL дерева
        }
    }

    // Добавление
    bool ZSet::add(const std::string& name, double score) {
        if (lookup(name) != nullptr) {
            return false;
        }
        auto* node = new ZNode(name, score);
        try {
            insert_into_both(node);
        }
        catch (...) {
            delete node;
            throw;
        }
        return true;
    }
    // Удаление
    bool ZSet::remove(const std::string& name) {
        uint64_t hash = hash_string(name);
        HNode* found = map_.find(name, hash, compare_names);
        if (!found) {
            return false;
        }
        auto* zn = node_from_hmap(found);
        remove_from_both(zn);
        delete zn;
        return true;
    }
    // Поиск элемента
    ZNode* ZSet::lookup(const std::string& name) const {
        uint64_t hash = hash_string(name);
        HNode* found = const_cast<StringHashMap&>(map_).find(name, hash, compare_names);
        return node_from_hmap(found);
    }
    // Поиск 1 элемента
    ZNode* ZSet::first_ge(double score, const std::string& name) const {
        ScoreNameComparator cmp;
        AVLNode* node = tree_.find_first_ge([&](const AVLNode* n) { return !cmp(n, score, name); }
            );
        return node_from_avl(node);
    }

    // Ищем следующий элемент в дереве
    ZNode* ZSet::next(ZNode* current) const {
        if (!current) return nullptr;
        AVLNode* next_node = tree_.next(&current->tree_node);
        return node_from_avl(next_node);
    }
    // Ищем предыдущий элемент в дереве
    ZNode* ZSet::prev(ZNode* current) const {
        if (!current) return nullptr;
        AVLNode* prev_node = tree_.prev(&current->tree_node);
        return node_from_avl(prev_node);
    }
    int64_t ZSet::rank(const std::string& name) const {
        ZNode* zn = lookup(name);
        if (!zn) return -1;
        return static_cast<int64_t>(tree_.rank(&zn->tree_node))
    }

    // ПОдсчет диапазон
    uint32_t ZSet::count(double min_score, double max_score) const {
        if (min_score > max_score) return 0;
        ScoreNameComparator cmp;
        // 1 первый элемент
        AVLNode* start = tree_.find_first_ge([&](const AVLNode* n) {
                auto* zn = node_from_avl(n);
                return zn->score >= min_score;
            }
        );
        if (!start) return 0;
        // Счет элементов
        uint32_t cnt = 0;
        for (AVLNode* cur = start; cur; cur = tree_.next(cur)) {
            auto* zn = node_from_avl(cur);
            if (zn->score > max_score) break;
            cnt++;
        }
        return cnt;
    }
    // Очистка
    void ZSet::clear() {
        std::vector<ZNode*> nodes; // Все узлы
        auto collector = [&](HNode* node) {
            nodes.push_back(node_from_hmap(node));
        };
        map_.for_each(collector);
        // Очистка
        map_.clear();
        tree_.clear();
        // Удаление
        for (auto* node : nodes) {
            delete node;
        }
    }
    template class HashMap<std::function<bool(const HNode*, const HNode*)>, std::function<uint64_t(const void*, size_t)>>;

}