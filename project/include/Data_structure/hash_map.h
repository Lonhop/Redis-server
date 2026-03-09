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
        explicit HashMap(HashFunc hash = default_hash, KeyEqual eq = default_equal) : hash_(std::move(hash)), equal_(std::move(eq)) {
            LOG_DEBUG("HashMap created with default config");
        }
        explicit HashMap(size_type initial_capacity,HashFunc hash = default_hash,KeyEqual eq = default_equal) : hash_(std::move(hash)),equal_(std::move(eq)) {
            if (initial_capacity > 0) {
                reserve(initial_capacity);
            }
            LOG_DEBUG("HashMap created with capacity {}", initial_capacity);
        }

        // Запрет копирования
        HashMap(const HashMap&) = delete;
        HashMap& operator=(const HashMap&) = delete;

        HashMap(HashMap&& other) noexcept: hash_(std::move(other.hash_)), equal_(std::move(other.equal_)) {
            std::unique_lock lock(other.mutex_);
            ht1_ = std::move(other.ht1_);
            ht2_ = std::move(other.ht2_);
            resizing_pos_ = other.resizing_pos_;
            total_operations_.store(other.total_operations_.load());
            // Сбрасываем источник
            other.ht1_ = HTab{};
            other.ht2_ = HTab{};
            other.resizing_pos_ = 0;
            LOG_DEBUG("HashMap moved");
        }

        HashMap& operator=(HashMap&& other) noexcept {
            if (this != &other) {
                std::unique_lock lock(mutex_);
                std::unique_lock other_lock(other.mutex_);

                ht1_ = std::move(other.ht1_);
                ht2_ = std::move(other.ht2_);
                resizing_pos_ = other.resizing_pos_;
                hash_ = std::move(other.hash_);
                equal_ = std::move(other.equal_);
                total_operations_.store(other.total_operations_.load());

                other.ht1_ = HTab{};
                other.ht2_ = HTab{};
                other.resizing_pos_ = 0;

                LOG_DEBUG("HashMap move assigned");
            }
            return *this;
        }

        ~HashMap() {
            std::unique_lock lock(mutex_);
            clear_unsafe();
            LOG_DEBUG("HashMap destroyed, total operations: {}", total_operations_.load());
        }

        // Количество элементов в таблице
        size_type size() const noexcept {
            std::shared_lock lock(mutex_);
            return ht1_.size + ht2_.size;
        }
        bool empty() const noexcept {
            std::shared_lock lock(mutex_);
            return (ht1_.size + ht2_.size) == 0;
        }
        // Текущая емкость таблицы
        size_type capacity() const noexcept {
            std::shared_lock lock(mutex_);
            if (ht2_.empty()) {
                return ht1_.empty() ? 0 : ht1_.capacity();
            }
            return ht1_.capacity() + ht2_.capacity();
        }

        // Коэффициент загрузки
        double load_factor() const noexcept {
            std::shared_lock lock(mutex_);
            size_type cap = capacity();
            if (cap == 0) return 0.0;
            return static_cast<double>(ht1_.size + ht2_.size) / cap;
        }

        // Зарезервировать емкость
        void reserve(size_type new_capacity) {
            if (new_capacity <= capacity()) {
                return;
            }
            // Округляем до степени двойки
            new_capacity = next_power_of_two(new_capacity);
            std::unique_lock lock(mutex_);

            if (ht1_.empty()) {
                init_htab(ht1_, new_capacity);
                LOG_DEBUG("Reserved capacity: {}", new_capacity);
            } else if (ht2_.empty()) {
                // Требуется рехеширование
                start_resizing_unsafe(new_capacity);
            } else {
                LOG_WARN("Cannot reserve capacity during resizing");
            }
        }
        // Вставка узла
        bool insert(HNode* node) {
            if (!node) {
                LOG_ERROR("HashMap::insert: node is null");
                throw std::invalid_argument("HashMap::insert: node cannot be null");
            }
            std::unique_lock lock(mutex_);
            total_operations_.fetch_add(1, std::memory_order_relaxed);

            // Проверка на существование
            if (lookup_unsafe(node) != nullptr) {
                LOG_DEBUG("Insert failed: node already exists (hcode={})", node->hcode);
                return false;
            }
            insert_unsafe(node);
            return true;
        }

        // Вставка или обновление
        std::pair<bool, HNode*> insert_or_replace(HNode* node) {
            if (!node) {
                LOG_ERROR("HashMap::insert_or_replace: node is null");
                throw std::invalid_argument("HashMap::insert_or_replace: node cannot be null");
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

        //  Поиск узла
        HNode* lookup(HNode* key) {
            if (!key) {
                LOG_ERROR("HashMap::lookup: key is null");
                throw std::invalid_argument("HashMap::lookup: key cannot be null");
            }

            std::shared_lock lock(mutex_);
            total_operations_.fetch_add(1, std::memory_order_relaxed);

            return lookup_unsafe(key);
        }

        // Поиск с предоставлением ключа
        template<typename T>
        HNode* find(const T& key, uint64_t hash,
                    bool (*eq)(const HNode*, const T&)) {
            if (!eq) {
                throw std::invalid_argument("HashMap::find: eq cannot be null");
            }
            std::shared_lock lock(mutex_);
            total_operations_.fetch_add(1, std::memory_order_relaxed);
            return find_unsafe(key, hash, eq);
        }

        // Удаление узла
        HNode* erase(HNode* key) {
            if (!key) {
                LOG_ERROR("HashMap::erase: key is null");
                throw std::invalid_argument("HashMap::erase: key cannot be null");
            }
            std::unique_lock lock(mutex_);
            total_operations_.fetch_add(1, std::memory_order_relaxed);
            return erase_unsafe(key);
        }

        // Удаление узла по ключу
        template<typename K>
        HNode* erase(const K& key, uint64_t hash,
                     bool (*eq)(const HNode*, const K&)) {
            if (!eq) {
                throw std::invalid_argument("HashMap::erase: eq cannot be null");
            }
            std::unique_lock lock(mutex_);
            total_operations_.fetch_add(1, std::memory_order_relaxed);
            return erase_unsafe(key, hash, eq);
        }

        // Очистка таблицы
        void clear() {
            std::unique_lock lock(mutex_);
            clear_unsafe();
            LOG_DEBUG("HashMap cleared");
        }
        // Обход всех элементов
        void for_each(std::function<void(HNode*)> f) const {
            if (!f) return;
            std::shared_lock lock(mutex_);
            for (const auto& slot : ht1_.tab) {
                for_each_in_slot(slot, f);
            }
            for (const auto& slot : ht2_.tab) {
                for_each_in_slot(slot, f);
            }
        }

        // Обход с возможностью раннего выхода
        bool for_each_until(std::function<bool(HNode*)> f) const {
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
        Stats get_stats() const {
            std::shared_lock lock(mutex_);
            Stats stats{
                .total_elements = ht1_.size + ht2_.size,
                .total_capacity = (ht1_.empty() ? 0 : ht1_.capacity()) +
                                  (ht2_.empty() ? 0 : ht2_.capacity()),
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
                stats.load_factor = static_cast<double>(stats.total_elements) /
                                   stats.total_capacity;
            }
            // Вычисляем длину цепочек
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
        // FNV-1a хеш функция по умолчанию
        static uint64_t default_hash(const void* data, size_t len) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            uint64_t h = 0xcbf29ce484222325ULL;
            for (size_t i = 0; i < len; ++i) {
                h ^= bytes[i];
                h *= 0x100000001b3ULL;
            }
            return h;
        }
        // сравнение указателей
        static bool default_equal(const HNode* a, const HNode* b) {
            return a == b;
        }
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
        static constexpr size_type next_power_of_two(size_type n) noexcept {
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

        // Инициализация таблицы
        void init_htab(HTab& htab, size_type n) {
            // проверка что не пустой
            if (n == 0) {
                LOG_ERROR("HashMap::init_htab: size cannot be zero");
                throw std::invalid_argument("HashMap::init_htab: size cannot be zero");
            }
            // проверка на размер
            if (n > MAX_CAPACITY) {
                LOG_ERROR("HashMap::init_htab: size {} exceeds maximum {}", n, MAX_CAPACITY);
                throw std::overflow_error("HashMap::init_htab: size too large");
            }
            // Проверка, что n - степень двойки
            if ((n & (n - 1)) != 0) {
                LOG_ERROR("HashMap::init_htab: size must be power of two, got {}", n);
                throw std::invalid_argument("HashMap::init_htab: size must be power of two");
            }
            // Проверка на переполнение при выделении памяти
            if (n > std::numeric_limits<size_t>::max() / sizeof(HNode*)) {
                LOG_ERROR("HashMap::init_htab: size {} too large for allocation", n);
                throw std::overflow_error("HashMap::init_htab: size too large for allocation");
            }
            try {
                htab.tab.assign(n, nullptr);
                htab.mask = n - 1;
                htab.size = 0;
                LOG_DEBUG("HashMap initialized with size {}", n);
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("HashMap::init_htab: allocation failed: {}", e.what());
                throw;
            }
        }

        // Освобождение таблицы без удаления узлов
        void free_htab(HTab& htab) noexcept {
            htab.tab.clear();
            htab.tab.shrink_to_fit();
            htab.mask = 0;
            htab.size = 0;
        }
        // Внутренняя вставка без проверок
        void insert_unsafe(HNode* node) {
            // Инициализация при первой вставке
            if (ht1_.tab.empty()) {
                init_htab(ht1_, DEFAULT_INITIAL_CAPACITY);
            }
            // Вставка в основную таблицу
            size_type pos = node->hcode & ht1_.mask;
            node->next = ht1_.tab[pos];
            ht1_.tab[pos] = node;
            ht1_.size++;
            // Проверка необходимости рехеширования
            if (ht2_.empty() && need_resizing_unsafe()) {
                start_resizing_unsafe();
            }
            // Прогрессивное рехеширование
            help_resizing_unsafe();
        }

        // Проверка необходимости рехеширования
        bool need_resizing_unsafe() const noexcept {
            if (ht1_.empty()) return false;
            size_type capacity = ht1_.capacity();
            size_type threshold = capacity * redis::config::HASH_MAP_MAX_LOAD_FACTOR;
            return ht1_.size >= threshold;
        }

        // Запуск рехеширования
        void start_resizing_unsafe(size_type new_capacity = 0) {
            if (!ht2_.empty()) {
                LOG_ERROR("HashMap::start_resizing: resizing already in progress");
                throw std::runtime_error("HashMap::start_resizing: resizing already in progress");
            }
            // Перемещаем основную таблицу в старую
            ht2_ = std::move(ht1_);
            size_type old_capacity = ht2_.capacity();
            if (new_capacity == 0) {
                new_capacity = old_capacity * 2;
            }
            // Проверка переполнения
            if (new_capacity < old_capacity || new_capacity > MAX_CAPACITY) {
                LOG_ERROR("HashMap::start_resizing: new capacity {} exceeds limit", new_capacity);
                throw std::overflow_error("HashMap::start_resizing: capacity too large");
            }
            // Округляем до степени двойки
            new_capacity = next_power_of_two(new_capacity);
            LOG_DEBUG("HashMap resizing from {} to {}", old_capacity, new_capacity);
            // Инициализируем новую таблицу
            init_htab(ht1_, new_capacity);
            resizing_pos_ = 0;
        }

        //  Прогрессивное рехеширование
        void help_resizing_unsafe() {
            if (ht2_.empty()) return;
            size_type nwork = 0;
            size_type max_iter = MAX_RESIZING_ITERATIONS;
            while (nwork < RESIZING_WORK_PER_OP && ht2_.size > 0 && --max_iter > 0) {
                // Проверка границ
                if (resizing_pos_ >= ht2_.tab.size()) {
                    resizing_pos_ = 0;
                }
                HNode** from = &ht2_.tab[resizing_pos_];
                if (!*from) {
                    ++resizing_pos_;
                    continue;
                }
                // Отцепляем первый узел из цепочки
                HNode* node = *from;
                *from = node->next;
                --ht2_.size;
                // Вставляем в основную таблицу
                size_type pos = node->hcode & ht1_.mask;
                node->next = ht1_.tab[pos];
                ht1_.tab[pos] = node;
                ++ht1_.size;
                ++nwork;
            }
            if (max_iter == 0) {
                LOG_ERROR("HashMap::help_resizing: max iterations exceeded");
                throw std::runtime_error("HashMap resizing stuck");
            }
            // Если всё переместили, освобождаем старую таблицу
            if (ht2_.size == 0) {
                LOG_DEBUG("HashMap resizing completed, freed old table");
                free_htab(ht2_);
                resizing_pos_ = 0;
            }
        }

        // Внутренний поиск по ключу
        HNode* lookup_unsafe(HNode* key) const {
            if (!key) return nullptr;
            // Поиск в основной таблице
            if (!ht1_.empty()) {
                size_type pos = key->hcode & ht1_.mask;
                for (HNode* cur = ht1_.tab[pos]; cur; cur = cur->next) {
                    if (cur->hcode == key->hcode && equal_(cur, key)) {
                        return cur;
                    }
                }
            }
            // Поиск в старой таблице
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
        //  Поиск по произвольному ключу
        template<typename K>
        HNode* find_unsafe(const K& key, uint64_t hash,
                           bool (*eq)(const HNode*, const K&)) const {
            // Поиск в основной таблице
            if (!ht1_.empty()) {
                size_type pos = hash & ht1_.mask;
                for (HNode* cur = ht1_.tab[pos]; cur; cur = cur->next) {
                    if (cur->hcode == hash && eq(cur, key)) {
                        return cur;
                    }
                }
            }
            // Поиск в старой таблице
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

        // Внутреннее удаление по ключу
        HNode* erase_unsafe(HNode* key) {
            // Поиск в основной таблице
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
            // Поиск в старой таблице
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
        //  Удаление по произвольному ключу
        template<typename K>
        HNode* erase_unsafe(const K& key, uint64_t hash,
                            bool (*eq)(const HNode*, const K&)) {
            // Поиск в основной таблице
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
            // Поиск в старой таблице
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

        //  Очистка без удаления узлов
        void clear_unsafe() noexcept {
            free_htab(ht1_);
            free_htab(ht2_);
            resizing_pos_ = 0;
        }

        // Обход одной цепочки
        static void for_each_in_slot(HNode* node, std::function<void(HNode*)>& f) {
            while (node) {
                f(node);
                node = node->next;
            }
        }
        // Обход одной цепочки с возможностью остановки
        static bool for_each_in_slot_until(HNode* node, std::function<bool(HNode*)>& f) {
            while (node) {
                if (!f(node)) {
                    return false;
                }
                node = node->next;
            }
            return true;
        }
    };
    // Хеш таблица с функцией сравнения по умолчанию
    using DefaultHashMap = HashMap<>;
    // Хеш таблица для строковых ключей
    using StringHashMap = HashMap<std::function<bool(const HNode*, const HNode*)>,std::function<uint64_t(const void*, size_t)>>;
}

#endif