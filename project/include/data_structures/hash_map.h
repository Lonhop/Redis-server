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

    class HashMap {
    public:
        using key_equal = KeyEqual;
        using hasher = HashFunc;
        using node_type = HNode;
        using size_type = size_t;

        // Константы
        static constexpr size_type DEFAULT_INITIAL_CAPACITY = 4;
        static constexpr size_type MIN_CAPACITY = 4;
        static constexpr size_type MAX_CAPACITY = size_type(1) << (sizeof(size_type) * 8 - 1);

        // Конструкторы
        explicit HashMap(HashFunc hash = default_hash, KeyEqual eq = default_equal);
        explicit HashMap(size_type initial_capacity, HashFunc hash = default_hash, KeyEqual eq = default_equal);

        // Запрет копирования
        HashMap(const HashMap&) = delete;
        HashMap& operator=(const HashMap&) = delete;

        HashMap(HashMap&& other) noexcept;
        HashMap& operator=(HashMap&& other) noexcept;

        ~HashMap();

        // Количество элементов в таблице
        size_type size() const noexcept;
        bool empty() const noexcept;
        // Текущая емкость таблицы
        size_type capacity() const noexcept;

        // Коэффициент загрузки
        double load_factor() const noexcept;

        // Зарезервировать емкость
        void reserve(size_type new_capacity);

        // Вставка узла
        bool insert(HNode* node);

        // Вставка или обновление
        std::pair<bool, HNode*> insert_or_replace(HNode* node);

        //  Поиск узла
        HNode* lookup(HNode* key);

        // Поиск с предоставлением ключа
        template<typename T>
        HNode* find(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&));

        // Удаление узла
        HNode* erase(HNode* key);

        // Удаление узла по ключу
        template<typename T>
        HNode* erase(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&));

        // Очистка таблицы
        void clear();

        // Обход всех элементов
        void for_each(std::function<void(HNode*)> f) const;

        // Обход с возможностью раннего выхода
        bool for_each_until(std::function<bool(HNode*)> f) const;

        // Получение статистики таблицы
        struct Stats {
            size_type total_elements;
            size_type total_capacity;
            size_type ht1_elements;
            size_type ht2_elements;
            size_type ht1_capacity;
            size_type ht2_capacity;
            double load_factor;
            size_type longest_chain;
            double average_chain;
            uint64_t total_operations;
            bool is_resizing;
        };

        // Получение статистики
        Stats get_stats() const;

        // FNV-1a хеш функция по умолчанию
        static uint64_t default_hash(const void* data, size_t len);

        // сравнение указателей
        static bool default_equal(const HNode* a, const HNode* b);

    private:
        // константы
        static constexpr size_type RESIZING_WORK_PER_OP = 128;
        static constexpr size_type MAX_RESIZING_ITERATIONS = 1'000'000;

        mutable std::shared_mutex mutex_;           // read-write lock
        HTab ht1_;                                   // основная таблица
        HTab ht2_;                                   // таблица для рехеширования
        mutable size_type resizing_pos_ = 0;         // текущая позиция миграции
        HashFunc hash_;                               // функция хеширования
        KeyEqual equal_;                              // функция сравнения
        mutable std::atomic<uint64_t> total_operations_{0}; // счетчик операций

        // Округление до степени двойки
        static constexpr size_type next_power_of_two(size_type n) noexcept;

        // Инициализация таблицы
        void init_htab(HTab& htab, size_type n);

        // Освобождение таблицы без удаления узлов
        void free_htab(HTab& htab) noexcept;

        // Внутренняя вставка без проверок
        void insert_unsafe(HNode* node);

        // Проверка необходимости рехеширования
        bool need_resizing_unsafe() const noexcept;

        // Запуск рехеширования
        void start_resizing_unsafe(size_type new_capacity = 0);

        //  Прогрессивное рехеширование
        void help_resizing_unsafe();

        // Внутренний поиск по ключу
        HNode* lookup_unsafe(HNode* key) const;

        //  Поиск по произвольному ключу
        template<typename T>
        HNode* find_unsafe(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&)) const;

        // Внутреннее удаление по ключу
        HNode* erase_unsafe(HNode* key);

        //  Удаление по произвольному ключу
        template<typename T>
        HNode* erase_unsafe(const T& key, uint64_t hash, bool (*eq)(const HNode*, const T&));

        //  Очистка без удаления узлов
        void clear_unsafe() noexcept;

        // Обход одной цепочки
        static void for_each_in_slot(HNode* node, std::function<void(HNode*)>& f);

        // Обход одной цепочки с возможностью остановки
        static bool for_each_in_slot_until(HNode* node, std::function<bool(HNode*)>& f);
    };

    // Хеш таблица с функцией сравнения по умолчанию
    using DefaultHashMap = HashMap<>;
    // Хеш таблица для строковых ключей
    using StringHashMap = HashMap<std::function<bool(const HNode*, const HNode*)>, std::function<uint64_t(const void*, size_t)>>;
}

#endif