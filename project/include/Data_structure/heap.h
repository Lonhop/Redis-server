#ifndef REDIS_SERVER_HEAP_H
#define REDIS_SERVER_HEAP_H

#include <vector>
#include <cstdint>
#include <stdexcept>

namespace redis::data_structures {

    struct HeapItem {
        uint64_t val; // время истечение (мкс)
        size_t* ref; // указатель на heapIdx Entry

        // Конструкторы
        HeapItem(uint64_t v = 0, size_t* r = nullptr) : val(v), ref(r) {}
        HeapItem(const HeapItem&) = default;
        HeapItem(HeapItem&&) noexcept = default;
        HeapItem& operator=(const HeapItem&) = default;
        HeapItem& operator=(HeapItem&&) noexcept = default;

        bool operator<(const HeapItem& other) const {return val < other.value; }
        bool operator>(const HeapItem& other) const {return val > other.value; }
        bool operator<=(const HeapItem& other) const {return val <= other.value; }
        bool operator>=(const HeapItem& other) const {return val >= other.value; }
    };

}

#endif //REDIS_SERVER_HEAP_H