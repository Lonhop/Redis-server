#ifndef REDIS_SERVER_HASH_MAP_H
#define REDIS_SERVER_HASH_MAP_H

#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include <optional>
#include <string>
#include <shared_mutex>
#include <atomic>
// использование header file
#include "config.h"
#include "utils/logger.h"

namespace redis::data_structures {

    // Проверка типов
    template<typename T>
    concept Hashable = requires(const T& t) {
        { std::hash<T>{}(t) } -> std::convertible_to<uint64_t>;
    };
    template<typename F, typename T>
    concept EqualityComparer = requires(F f, const T* a, const T* b) {
        { f(a, b) } -> std::convertible_to<bool>;
    };

    //  Интрузивный узел хеш таблицы
    struct HNode {
        HNode* next = nullptr;      // указатель на следующий узел в цепочке
        uint64_t hcode = 0;          // хеш-код ключа
        bool is_valid() const noexcept {
            return this != nullptr;
        }
    };

    // Таблица фиксированного размера
    struct HTab {
        std::vector<HNode*> tab;     // массив указателей на начала цепочек
        size_t mask = 0;              // tab.size() - 1
        size_t size = 0;              // количество узлов в этой таблице
        bool empty() const noexcept {
            return size == 0;
        }
        size_t capacity() const noexcept {
            return mask + 1;
        }
        bool is_valid() const noexcept {
            return tab.empty() || (mask == tab.size() - 1 && (capacity() & (capacity() - 1)) == 0);
        }
    };
    template<EqualityComparer<HNode> KeyEqual = std::function<bool(const HNode*, const HNode*)>,typename HashFunc = std::function<uint64_t(const void*, size_t)>>
}

#endif