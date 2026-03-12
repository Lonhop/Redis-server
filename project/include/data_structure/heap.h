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

        bool operator<(const HeapItem& other) const {return val < other.val; }
        bool operator>(const HeapItem& other) const {return val > other.val; }
        bool operator<=(const HeapItem& other) const {return val <= other.val; }
        bool operator>=(const HeapItem& other) const {return val >= other.val; }
    };

    class Heap {
    public:
        Heap() = default;
        ~Heap() = default;

        // Запрепт копирования
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;

        // Перемещение
        Heap(Heap&& other) noexcept;
        Heap& operator=(Heap&& other) noexcept;


        // Операции
        void push(uint64_t val, size_t* ref);
        void update(size_t pos, uint64_t new_val);
        void erase(size_t pos);

        // Доступ к 1 элементу
        const HeapItem& top() const;
        HeapItem& top();

        // Проверка на пустоту, получение размера и очищение
        bool empty() const noexcept { return heap_.empty(); }
        size_t size() const noexcept { return heap_.size(); }
        void clear() noexcept { heap_.clear(); }

        // Поиск
        bool contains(size_t pos) const noexcept;
        const HeapItem& at(size_t size) const;
        HeapItem& at(size_t pos);

        // Итераторы
        using iterator = std::vector<HeapItem>::iterator;
        using const_iterator = std::vector<HeapItem>::const_iterator;

        // Функции доступа итераторов
        iterator begin() noexcept { return heap_.begin(); }
        iterator end() noexcept { return heap_.end(); }
        const_iterator begin() const noexcept { return heap_.begin(); }
        const_iterator end() const noexcept { return heap_.end(); }
        const_iterator cbegin() const noexcept { return heap_.cbegin(); }
        const_iterator cend() const noexcept { return heap_.cend(); }

    private:
        std::vector<HeapItem> heap_;

        static size_t parent(size_t t) noexcept { return (t - 1) / 2; }
        static size_t left(size_t t) noexcept { return t * 2 + 1; }
        static size_t right(size_t t) noexcept { return t * 2 + 2; }

        void up(size_t pos);
        void down(size_t pos);
        void swap_items(size_t i, size_t j);
        void validate_pos(size_t pos) const;

    };
}

#endif //REDIS_SERVER_HEAP_H