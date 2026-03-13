#include "data_structures/hash_map.h"
#include "utils/logger.h"
#include <iostream>

namespace redis::data_structures {
    HTab::HTab(size_t n) {
        if (n > 0 && (n & (n - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
        tab = new HNode*[n]();
        mask = n ? (n - 1) : 0;
        size = 0;
    }
    template<typename KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>::HashMap(HashFunc hash, KeyEqual eq, size_type initial_capacity) : ht1_(initial_capacity), hash_func_(std::move(hash)), equal_func_(std::move(eq)) {
        LOG_DEBUG("HashMap created");
    }
    template<typename KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>::~HashMap() {
        delete[] ht1_.tab;
        delete[] ht2_.tab;
    }
    template<typename KeyEqual, typename HashFunc>
    HashMap<KeyEqual, HashFunc>::HashMap(HashMap&& other) noexcept : ht1_(other.ht1_), ht2_(other.ht2_), resizing_pos_(other.resizing_pos_), hash_func_(std::move(other.hash_func_)), equal_func_(std::move(other.equal_func_)) {
        other.ht1_.tab = nullptr;
        other.ht2_.tab = nullptr;
    }
    template<typename KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::start_resizing_unsafe() {
        ht2_ = ht1_;
        ht1_ = HTab(ht2_.capacity() * 2);
        resizing_pos_ = 0;
        LOG_DEBUG("HashMap resizing started");
    }
    template<typename KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::move_nodes(size_type n) {
        while (n-- && ht2_.size > 0) {
            while (resizing_pos_ <= ht2_.mask && !ht2_.tab[resizing_pos_]) {
                resizing_pos_++;
            }

            if (resizing_pos_ > ht2_.mask) break;

            HNode** from = &ht2_.tab[resizing_pos_];
            while (*from) {
                HNode* node = *from;
                *from = node->next;

                size_t pos = node->hcode & ht1_.mask;
                node->next = ht1_.tab[pos];
                ht1_.tab[pos] = node;

                ht2_.size--;
                ht1_.size++;
            }
        }

        if (ht2_.size == 0 && ht2_.tab) {
            delete[] ht2_.tab;
            ht2_ = HTab();
        }
    }
    template<typename KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::help_resizing() {
        if (is_resizing()) move_nodes(128);
    }
    template<typename KeyEqual, typename HashFunc>
    void HashMap<KeyEqual, HashFunc>::insert(HNode* node) {
        if (!is_resizing() && (ht1_.capacity() > 0 && (double)ht1_.size / ht1_.capacity() > 0.8)) {
            start_resizing_unsafe();
        }
        size_t pos = node->hcode & ht1_.mask;
        node->next = ht1_.tab[pos];
        ht1_.tab[pos] = node;
        ht1_.size++;
        help_resizing();
        total_operations_++;
    }
    template<typename KeyEqual, typename HashFunc>
    HNode* HashMap<KeyEqual, HashFunc>::lookup(HNode* key) {
        help_resizing();
        auto search = [&](const HTab &ht) -> HNode* {
            if (!ht.tab) return nullptr;
            size_t pos = key->hcode & ht.mask;
            HNode* cur = ht.tab[pos];
            while (cur) {
                if (cur->hcode == key->hcode && equal_func_(cur, key)) return cur;
                cur = cur->next;
            }
            return nullptr;
        };
        HNode* res = search(ht1_);
        if (!res && is_resizing()) res = search(ht2_);
        return res;
    }
    template<typename KeyEqual, typename HashFunc>
    HNode* HashMap<KeyEqual, HashFunc>::pop(HNode* key) {
        help_resizing();
        auto extract = [&](HTab &ht) -> HNode* {
            if (!ht.tab) return nullptr;
            size_t pos = key->hcode & ht.mask;
            HNode** prev = &ht.tab[pos];
            while (*prev) {
                if ((*prev)->hcode == key->hcode && equal_func_(*prev, key)) {
                    HNode* found = *prev;
                    *prev = found->next;
                    ht.size--;
                    return found;
                }
                prev = &(*prev)->next;
            }
            return nullptr;
        };
        HNode* res = extract(ht1_);
        if (!res && is_resizing()) res = extract(ht2_);
        return res;
    }
    template class HashMap<std::function<bool(const HNode*, const HNode*)>, std::function<uint64_t(const void*, size_t)>>;
}