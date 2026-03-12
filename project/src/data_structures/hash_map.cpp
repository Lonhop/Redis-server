#include "data_structures/hash_map.h"
#include <limits>
#include <algorithm>

namespace redis::data_structures {

    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    uint64_t HashMap<KeyEqual, HashFunc>::default_hash(const void* data, size_t len) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    bool HashMap<KeyEqual, HashFunc>::default_equal(const HNode* a, const HNode* b) {
        return a == b;
    }

    // Конструкторы
    template<EqualityCamparer<HNode> KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>::HashMap(HashFunc hash, KeyEqual eq) : hash_(std::move(hash)), equal_(std::move(eq)) {
        LOG_DEBUG("HashMap was created by default");
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>::HashMap(size_type initial_capacity, HashFunc hash, KeyEqual eq) : hash_(std::move(hash)), equal_(std::move(eq)) {
        if (initial_capacity > 0) {
            reserve(initial_capacity);
        }
        LOG_DEBUG("HashMap was created with capacity: ", initial_capacity);
    }
    template<EqualityCamparer<HNode> KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>::HashMap(HashMap&& other) noexcept : hash_(std::move(other.hash_)), equal_(std::move(other.equal_)) {
        std::unique_lock lock(other.mutex_);
        ht1_ = std::move(other.ht1_);
        ht2_ = std::move(other.ht2_);
        resizing_pos_ = other.resizing_pos_;
        total_operations_.store(other.total_operations_.load());
        // Сброс
        other.ht1_ = HTab{};
        other.ht2_ = HTab{};
        other.resizing_pos_ = 0;
        LOG_DEBUG("HashMap was moved");
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>& HashMap<KeyEqual, HashFunc>::operator=(HashMap&& other) noexcept {
        if (this != &other) {
            std::unique_lock lock(mutex_);
            std::unique_lock other_lock(other.mutex_);
            ht1_ = std::move(other.ht1_);
            ht2_ = std::move(other.ht2_);
            resizing_pos_ = other.resizing_pos_;
            hash_ = std::move(other.hash_);
            equal_ = std::move(other.equal_);
            total_operations_.store(other.total_operations_.load());
            // Сброс
            other.ht1_ = HTab{};
            other.ht2_ = HTab{};
            other.resizing_pos_ = 0;
            LOG_DEBUG("HashMap was moved assigned");
        }
        return *this;
    }
    // Деструктор
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>:: ~HashMap() {
        std::unique_lock lock(mutex_);
        clear_unsafe();
        LOG_DEBUG("HashMap was destroyed, total operations: ", total operations_.load())
    }

    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    constexpr typename HashMap<KeyEqual, HashFunc>::size_type
    HashMap<KeyEqual, HashFunc>::next_power_of_two(size_type n) noexcept {
        if (n == 0) return MIN_CAPACITY;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }
    // Иницилизировани
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::init_htab(HTab& htab, size_type n) {
        if (n == 0) {
            LOG_ERROR("HashMap::init_htab: 0 size");
            throw std::invalid_argument("HashMap::init_htab: 0 size");
        }
        if (n > MAX_CAPACITY) {
            LOG_ERROR("HashMap::init_htab: size more than capacity: ", n);
            throw std::overflow_error("HashMap::init_htab: size more than capacity");
        }
        if ((n & (n - 1)) != 0) {
            LOG_ERROR("HashMap::init_htab: not power of 2");
            throw std::invalid_argument("HashMap::init_htab: not power of 2");
        }
        if (n > std::numeric_limits<size_t>::max() / sizeof(HNode*)) {
            LOG_ERROR("HashMap::init_htab: size larger than allocation: ", n);
            throw std::overflow_error("HashMap::init_htab: size larger than allocation");
        }
        try {
            htab.tab.assign(n, nullptr);
            htab.mask = n - 1;
            htab.size = 0;
            LOG_DEBUG("HashMap initialized with size: ", n);
        }
        catch (const std::bad_alloc& e) {
            LOG_ERROR("HashMap::init_htab: allocation failed: ", e.what());
            throw;
        }
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::free_htab(HTab& htab) noexcept {
        htab.tab.clear();
        htab.tab.shrink_to_fit();
        htab.mask = 0;
        htab.size = 0;
    }

    // Размер
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    typename HashMap<KeyEqual, HashFunc>::size_type
    HashMap<KeyEqual, HashFunc>::size() const noexcept {
        std::shared_lock lock(mutex_);
        return ht1_.size + ht2_.size;
    }
    // Проверка на пустоту
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    bool HashMap<KeyEqual, HashFunc>::empty() const noexcept {
        std::shared_lock lock(mutex_);
        return (ht1_.size + ht2_.size) == 0;
    }
    // Объем 2 таблиц
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    typename HashMap<KeyEqual, HashFunc>::size_type
    HashMap<KeyEqual, HashFunc>::capacity() const noexcept {
        std::shared_lock lock(mutex_);
        if (ht2_.empty()) {
            return ht1_.empty() ? 0 : ht1_.capacity();
        }
        return ht1_.capacity() + ht2_.capacity();
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    double HashMap<KeyEqual, HashFunc>::load_factor() const noexcept {
        std::shared_lock lock(mutex_);
        size_type cap = capacity();
        if (cap == 0) return 0.0;
        return static_cast<double>(ht1_.size + ht2_.size) / cap;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::reserve(size_type new_capacity) {
        if (new_capacity <= capacity()) {
            return;
        }
        new_capacity = next_power_of_two(new_capacity);
        std::unique_lock lock(mutex_);
        if (ht1_.empty()) {
            init_htab(ht1_, new_capacity);
            LOG_DEBUG("Reserved capacity: ", new_capacity);
        }
        else if (ht2_.empty()) {
            start_resizing_unsafe(new_capacity);
        }
        else {
            LOG_WARN("Can't reserve capacity ");
        }
    }

    // Проверка
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    bool HashMap<KeyEqual, HashFunc>::need_resizing_unsafe() const noexcept {
        if (ht1_.empty()) return false;
        size_type capacity = ht1_.capacity();
        size_type threshold = capacity * redis::config::HASH_MAP_MAX_LOAD_FACTOR;
        return ht1_.size >= threshold;
    }
    // Вставка
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::start_resizing_unsafe(size_type new_capacity) {
        if (!ht2_.empty()) {
            LOG_ERROR("HashMap::start_resizing: already in process");
            throw std::runtime_error("HashMap::start_resizing: already in process");
        }
        ht2_ = std::move(ht1_);
        size_type old_capacity = ht2_.capacity();
        if (new_capacity == 0) {
            new_capacity = old_capacity * 2;
        }
        if (new_capacity < old_capacity || new_capacity > MAX_CAPACITY) {
            LOG_ERROR("HashMap::start_resizing: exceeds limit");
            throw std::overflow_error("HashMap::start_resizing: too large");
        }
        new_capacity = next_power_of_two(new_capacity);
        LOG_DEBUG("HashMap resizing");
        init_htab(ht1_, new_capacity);
        resizing_pos_ = 0;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::help_resizing_unsafe() {
        if (ht2_.empty()) return;
        size_type nwork = 0;
        size_type max_iter = MAX_RESIZING_ITERATIONS;
        while (nwork < RESIZING_WORK_PER_OP && ht2_.size > 0 && --max_iter > 0) {
            if (resizing_pos_ >= ht2_.tab.size()) {
                resizing_pos_ = 0;
            }
            HNode** from = &ht2_.tab[resizing_pos_];
            if (!*from) {
                ++resizing_pos_;
                continue;
            }
            HNode* node = *from;
            *from = node->next;
            --ht2_.size;
            size_type pos = node->hcode & ht1_.mask;
            node->next = ht1_.tab[pos];
            ht1_.tab[pos] = node;
            ++ht1_.size;
            ++nwork;
        }
        if (max_iter == 0) {
            LOG_ERROR("HashMap::help_resizing: max iterations");
            throw std::runtime_error("HashMap max iterations");
        }
        if (ht2_.size == 0) {
            LOG_DEBUG("HashMap resizing completed");
            free_htab(ht2_);
            resizing_pos_ = 0;
        }
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::insert_unsafe(HNode* node) {
        if (ht1_.tab.empty()) {
            init_htab(ht1_, DEFAULT_INITIAL_CAPACITY);
        }
        size_type pos = node->hcode & ht1_.mask;
        node->next = ht1_.tab[pos];
        ht1_.tab[pos] = node;
        ++ht1_.size;
        if (ht2_.empty() && need_resizing_unsafe()) {
            start_resizing_unsafe();
        }
        help_resizing_unsafe();
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    bool HashMap<KeyEqual, HashFunc>::insert(HNode* node) {
        if (!node) {
            LOG_ERROR("HashMap::insert: node is nullptr");
            throw std::invalid_argument("HashMap::insert: node can't be nullptr");
        }
        std::unique_lock lock(mutex_);
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        if (lookup_unsafe(node) != nullptr) {
            LOG_DEBUG("Insert failed: already exist");
            return false;
        }
        insert_unsafe(node);
        return true;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    std::pair<bool, HNode*> HashMap<KeyEqual, HashFunc>::insert_or_replace(HNode* node) {
        if (!node) {
            LOG_ERROR("HashMap::insert_or_replace: node is nullptr");
            throw std::invalid_argument("HashMap::insert_or_replace: node can't be null");
        }
        std::unique_lock lock(mutex_);
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        HNode* existing = lookup_unsafe(node);
        if (existing) {
            return {false, existing};
        }
        insert_unsafe(node);
        return {true, nullptr};
    }

    // Поиск
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HNode* HashMap<KeyEqual, HashFunc>::lookup_unsafe(HNode* key) const {
        if (!key) return nullptr;
        if (!ht1_.empty()) {
            size_type pos = key->hcode & ht1_.mask;
            for (HNode* cur = ht1_.tab[pos]; cur; cur = cur->next) {
                if (cur->hcode == key->hcode && equal_(cur, key)) {
                    return cur;
                }
            }
        }
        if (!ht2_.empty()) {
            size_type pos = key->hcode & ht2_.mask;
            for (HNode* cur = ht2_.tab[pos]; cur; cur = cur->next) {
                if (cur->hcode == key->hcode && equal_(cur, key)) {
                    return cur;
                }
            }
        }
        return nullptr;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HNode* HashMap<KeyEqual, HashFunc>::lookup(HNode* key) {
        if (!key) {
            LOG_ERROR("HashMap::lookup: key is nullptr");
            throw std::invalid_argument("HashMap::lookup: key can't be nullptr");
        }
        std::shared_lock lock(mutex_);
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        return lookup_unsafe(key);
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    template<typename T>
    HNode* HashMap<KeyEqual, HashFunc>::find_unsafe(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&)) const {
        if (!ht1_.empty()) {
            size_type pos = hash & ht1_.mask;
            for (HNode* cur = ht1_.tab[pos]; cur; cur = cur->next) {
                if (cur->hcode == hash && eq(cur, key)) {
                    return cur;
                }
            }
        }
        if (!ht2_.empty()) {
            size_type pos = hash & ht2_.mask;
            for (HNode* cur = ht2_.tab[pos]; cur; cur = cur->next) {
                if (cur->hcode == hash && eq(cur, key)) {
                    return cur;
                }
            }
        }
        return nullptr;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    template<typename T>
    HNode* HashMap<KeyEqual, HashFunc>::find(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&)) {
        if (!eq) {
            throw std::invalid_argument("HashMap::find: can't be null");
        }
        std::shared_lock lock(mutex_);
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        return find_unsafe(key, hash, eq);
    }

    // Удаление
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HNode* HashMap<KeyEqual, HashFunc>::erase_unsafe(HNode* key) {
        if (!ht1_.empty()) {
            size_type pos = key->hcode & ht1_.mask;
            HNode** from = &ht1_.tab[pos];
            for (HNode* cur = *from; cur; from = &cur->next, cur = *from) {
                if (cur->hcode == key->hcode && equal_(cur, key)) {
                    *from = cur->next;
                    --ht1_.size;
                    return cur;
                }
            }
        }
        if (!ht2_.empty()) {
            size_type pos = key->hcode & ht2_.mask;
            HNode** from = &ht2_.tab[pos];
            for (HNode* cur = *from; cur; from = &cur->next, cur = *from) {
                if (cur->hcode == key->hcode && equal_(cur, key)) {
                    *from = cur->next;
                    --ht2_.size;
                    return cur;
                }
            }
        }
        return nullptr;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    HNode* HashMap<KeyEqual, HashFunc>::erase(HNode* key) {
        if (!key) {
            LOG_ERROR("HashMap::erase: key is nullptr");
            throw std::invalid_argument("HashMap::erase: key can't be nullptr");
        }
        std::unique_lock lock(mutex_);
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        return erase_unsafe(key);
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    template<typename T>
    HNode* HashMap<KeyEqual, HashFunc>::erase_unsafe(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&)) {
        if (!ht1_.empty()) {
            size_type pos = hash & ht1_.mask;
            HNode** from = &ht1_.tab[pos];
            for (HNode* cur = *from; cur; from = &cur->next, cur = *from) {
                if (cur->hcode == hash && eq(cur, key)) {
                    *from = cur->next;
                    --ht1_.size;
                    return cur;
                }
            }
        }
        if (!ht2_.empty()) {
            size_type pos = hash & ht2_.mask;
            HNode** from = &ht2_.tab[pos];
            for (HNode* cur = *from; cur; from = &cur->next, cur = *from) {
                if (cur->hcode == hash && eq(cur, key)) {
                    *from = cur->next;
                    --ht2_.size;
                    return cur;
                }
            }
        }
        return nullptr;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    template<typename T>
    HNode* HashMap<KeyEqual, HashFunc>::erase(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&)) {
        if (!eq) {
            throw std::invalid_argument("HashMap::erase: can't be nullptr");
        }
        std::unique_lock lock(mutex_);
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        return erase_unsafe(key, hash, eq);
    }

    //  Очистка
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::clear_unsafe() noexcept {
        free_htab(ht1_);
        free_htab(ht2_);
        resizing_pos_ = 0;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::clear() {
        std::unique_lock lock(mutex_);
        clear_unsafe();
        LOG_DEBUG("HashMap was cleared");
    }

    // Обходы
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::for_each_in_slot(HNode* node, std::function<void(HNode*)>& f) {
        while (node) {
            f(node);
            node = node->next;
        }
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::for_each(std::function<void(HNode*)> f) const {
        if (!f) return;
        std::shared_lock lock(mutex_);
        for (const auto& slot : ht1_.tab) {
            for_each_in_slot(slot, f);
        }
        for (const auto& slot : ht2_.tab) {
            for_each_in_slot(slot, f);
        }
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    bool HashMap<KeyEqual, HashFunc>::for_each_in_slot_until(HNode* node, std::function<bool(HNode*)>& f) {
        while (node) {
            if (!f(node)) {
                return false;
            }
            node = node->next;
        }
        return true;
    }
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    bool HashMap<KeyEqual, HashFunc>::for_each_until(std::function<bool(HNode*)> f) const {
        if (!f) return true;
        std::shared_lock lock(mutex_);
        for (const auto& slot : ht1_.tab) {
            if (!for_each_in_slot_until(slot, f)) {
                return false;
            }
        }
        for (const auto& slot : ht2_.tab) {
            if (!for_each_in_slot_until(slot, f)) {
                return false;
            }
        }
        return true;
    }

    // Статистика
    template<EqualityComparer<HNode> KeyEqual, typename HashFunc>
    typename HashMap<KeyEqual, HashFunc>::Stats
    HashMap<KeyEqual, HashFunc>::get_stats() const {
        std::shared_lock lock(mutex_);
        Stats stats{
            .total_elements = ht1_.size + ht2_.size,
            .total_capacity = (ht1_.empty() ? 0 : ht1_.capacity()) + (ht2_.empty() ? 0 : ht2_.capacity()),
            .ht1_elements = ht1_.size,
            .ht2_elements = ht2_.size,
            .ht1_capacity = ht1_.empty() ? 0 : ht1_.capacity(),
            .ht2_capacity = ht2_.empty() ? 0 : ht2_.capacity(),
            .load_factor = 0.0,
            .longest_chain = 0,
            .average_chain = 0.0,
            .total_operations = total_operations_.load(),
            .is_resizing = !ht2_.empty()
        };
        if (stats.total_capacity > 0) {
            stats.load_factor = static_cast<double>(stats.total_elements) / stats.total_capacity;
        }
        size_type total_chains = 0;
        size_type total_nodes_in_chains = 0;
        auto update_chain_stats = [&](const std::vector<HNode*>& tab) {
            for (const auto& head : tab) {
                size_type chain_len = 0;
                for (auto* node = head; node; node = node->next) {
                    ++chain_len;
                }
                if (chain_len > 0) {
                    ++total_chains;
                    total_nodes_in_chains += chain_len;
                    if (chain_len > stats.longest_chain) {
                        stats.longest_chain = chain_len;
                    }
                }
            }
        };
        if (!ht1_.tab.empty()) update_chain_stats(ht1_.tab);
        if (!ht2_.tab.empty()) update_chain_stats(ht2_.tab);
        if (total_chains > 0) {
            stats.average_chain = static_cast<double>(total_nodes_in_chains) / total_chains;
        }
        return stats;
    }
    // Создание шаблона
    template class HashMap<std::function<bool(const HNode*, const HNode*)>, std::function<uint64_t(const void*,size_t)>>;
}