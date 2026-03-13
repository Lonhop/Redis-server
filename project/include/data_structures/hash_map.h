#ifndef REDIS_SERVER_HASH_MAP_H
#define REDIS_SERVER_HASH_MAP_H

#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>
#include <stdexcept>

namespace redis::data_structures {

    struct HNode {
        HNode* next = nullptr;
        uint32_t hcode = 0;
    };

    struct HTab {
        HNode** tab = nullptr;
        size_t mask = 0;
        size_t size = 0;

        HTab() = default;
        HTab(size_t n);
        size_t capacity() const { return tab ? mask + 1 : 0; }
    };

    template<typename T>
    using EqualityComparer = std::function<bool(const T*, const T*)>;

    template<
        typename KeyEqual = EqualityComparer<HNode>,
        typename HashFunc = std::function<uint64_t(const void*, size_t)>
    >
    class HashMap {
    public:
        using size_type = size_t;

        explicit HashMap(HashFunc hash, KeyEqual eq, size_type initial_capacity = 4);
        ~HashMap();

        HashMap(const HashMap&) = delete;
        HashMap& operator=(const HashMap&) = delete;
        HashMap(HashMap&& other) noexcept;

        void insert(HNode* node);
        HNode* lookup(HNode* key);
        HNode* pop(HNode* key);

        size_type size() const { return ht1_.size + ht2_.size; }

    private:
        void help_resizing();
        void start_resizing_unsafe();
        void move_nodes(size_type n);
        bool is_resizing() const { return ht2_.tab != nullptr; }

        HTab ht1_;
        HTab ht2_;
        size_type resizing_pos_ = 0;

        HashFunc hash_func_;   // Проверьте это имя
        KeyEqual equal_func_;  // Проверьте это имя
        std::atomic<uint64_t> total_operations_{0};
    };

}
#endif