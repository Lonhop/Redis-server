#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <thread>

#include "data_structures/heap.h"

namespace redis::data_structures {
    class HeapTest : public ::testing::Test {
    protected:
        void SetUp() override {
            for (auto& idx : indices) {
                idx = SIZE_MAX;
            }
        }
        static constexpr size_t MAX_INDICES = 2048;
        size_t indices[MAX_INDICES] = {SIZE_MAX};
        Heap heap;
    };

    // Вставка
    TEST_F(HeapTest, BasicPushAndSize) {
        EXPECT_TRUE(heap.empty());
        EXPECT_EQ(heap.size(), 0);
        heap.push(100, &indices[0]);
        EXPECT_FALSE(heap.empty());
        EXPECT_EQ(heap.size(), 1);
        EXPECT_EQ(indices[0], 0);
        heap.push(200, &indices[1]);
        EXPECT_EQ(heap.size(), 2);
        EXPECT_EQ(indices[1], 1);
    }
    // Получение top
    TEST_F(HeapTest, Top) {
        heap.push(300, &indices[0]);
        heap.push(100, &indices[1]);
        heap.push(200, &indices[2]);

        EXPECT_EQ(heap.top().val, 100);
        EXPECT_EQ(heap.top().ref, &indices[1]);

        heap.push(50, &indices[3]);
        EXPECT_EQ(heap.top().val, 50);
        EXPECT_EQ(heap.top().ref, &indices[3]);
    }
    // проверка на пустоту
    TEST_F(HeapTest, EmptyHeap) {
        EXPECT_THROW(heap.top(), std::runtime_error);

        heap.push(100, &indices[0]);
        EXPECT_NO_THROW(heap.top());

        heap.erase(0);
        EXPECT_THROW(heap.top(), std::runtime_error);
    }
    // Удаление
    TEST_F(HeapTest, Erase) {
        heap.push(300, &indices[0]);
        heap.push(100, &indices[1]);
        heap.push(200, &indices[2]);
        heap.push(400, &indices[3]);
        EXPECT_EQ(heap.size(), 4);
        EXPECT_EQ(heap.top().val, 100);
        EXPECT_EQ(heap.top().ref, &indices[1]);

        // Найдём позицию элемента со значением 100
        size_t pos_100 = SIZE_MAX;
        for (size_t i = 0; i < heap.size(); ++i) {
            if (heap.at(i).val == 100) {
                pos_100 = i;
                break;
            }
        }
        ASSERT_NE(pos_100, SIZE_MAX);
        heap.erase(pos_100);
        EXPECT_EQ(heap.size(), 3);
        EXPECT_EQ(heap.top().val, 200);

        bool found = false;
        for (size_t i = 0; i < heap.size(); ++i) {
            if (heap.at(i).ref == &indices[1]) {
                found = true;
                break;
            }
        }
        EXPECT_FALSE(found);
    }
    // Обновление значения
    TEST_F(HeapTest, Update) {
        heap.push(300, &indices[0]);
        heap.push(100, &indices[1]);
        heap.push(200, &indices[2]);
        EXPECT_EQ(heap.top().val, 100);
        EXPECT_EQ(heap.top().ref, &indices[1]);

        // Обновляем элемент со значением 300
        size_t pos_300 = SIZE_MAX;
        for (size_t i = 0; i < heap.size(); ++i) {
            if (heap.at(i).val == 300) {
                pos_300 = i;
                break;
            }
        }
        ASSERT_NE(pos_300, SIZE_MAX);
        heap.update(pos_300, 50);
        EXPECT_EQ(heap.top().val, 50);
        EXPECT_EQ(heap.top().ref, &indices[0]);

        // Обновляем элемент со значением 100
        size_t pos_100 = SIZE_MAX;
        for (size_t i = 0; i < heap.size(); ++i) {
            if (heap.at(i).val == 100) {
                pos_100 = i;
                break;
            }
        }
        ASSERT_NE(pos_100, SIZE_MAX);
        heap.update(pos_100, 500);
        EXPECT_EQ(heap.top().val, 50);
        EXPECT_EQ(heap.top().ref, &indices[0]);
    }
    // проверка ссылок
    TEST_F(HeapTest, ReferenceIntegrity) {
        heap.push(300, &indices[0]);
        heap.push(100, &indices[1]);
        heap.push(200, &indices[2]);

        for (size_t i = 0; i < heap.size(); ++i) {
            EXPECT_EQ(*heap.at(i).ref, i);
        }

        // Обновляем элемент с 300 до 50
        size_t pos_300 = SIZE_MAX;
        for (size_t i = 0; i < heap.size(); ++i) {
            if (heap.at(i).val == 300) {
                pos_300 = i;
                break;
            }
        }
        ASSERT_NE(pos_300, SIZE_MAX);
        heap.update(pos_300, 50);

        for (size_t i = 0; i < heap.size(); ++i) {
            EXPECT_EQ(*heap.at(i).ref, i);
        }
    }

    // Проверка на позицию
    TEST_F(HeapTest, InvalidPosition) {
        heap.push(100, &indices[0]);
        EXPECT_THROW(heap.update(5, 200), std::out_of_range);
        EXPECT_THROW(heap.erase(5), std::out_of_range);
        EXPECT_THROW(heap.at(5), std::out_of_range);
        EXPECT_NO_THROW(heap.update(0, 150));
        EXPECT_NO_THROW(heap.erase(0));
    }
    // Проверка contains
    TEST_F(HeapTest, Contains) {
        heap.push(100, &indices[0]);
        heap.push(200, &indices[1]);
        EXPECT_TRUE(heap.contains(0));
        EXPECT_TRUE(heap.contains(1));
        EXPECT_FALSE(heap.contains(2));
        EXPECT_FALSE(heap.contains(100));

        heap.erase(0);  // удаляем корень

        EXPECT_TRUE(heap.contains(0));  // на позиции 0 теперь другой элемент
        EXPECT_FALSE(heap.contains(1)); // размер стал 1
    }
    // Доступ по индексу
    TEST_F(HeapTest, At) {
        heap.push(100, &indices[0]);
        heap.push(200, &indices[1]);
        EXPECT_EQ(heap.at(0).val, 100);
        EXPECT_EQ(heap.at(1).val, 200);
        EXPECT_EQ(heap.at(0).ref, &indices[0]);
        EXPECT_EQ(heap.at(1).ref, &indices[1]);
        heap.at(0).val = 150;
        EXPECT_EQ(heap.at(0).val, 150);
    }
    // Очистка
    TEST_F(HeapTest, Clear) {
        heap.push(100, &indices[0]);
        heap.push(200, &indices[1]);
        heap.push(300, &indices[2]);
        EXPECT_EQ(heap.size(), 3);
        EXPECT_FALSE(heap.empty());
        heap.clear();
        EXPECT_EQ(heap.size(), 0);
        EXPECT_TRUE(heap.empty());
        EXPECT_THROW(heap.top(), std::runtime_error);
    }
    // Проверка свойства min-heap
    TEST_F(HeapTest, MinHeapProperty) {
        std::vector<uint64_t> values = {5, 3, 8, 1, 9, 2, 7, 4, 6};
        for (size_t i = 0; i < values.size(); ++i) {
            heap.push(values[i], &indices[i]);
        }
        for (size_t i = 0; i < heap.size(); ++i) {
            size_t left = 2 * i + 1;
            size_t right = 2 * i + 2;
            if (left < heap.size()) {
                EXPECT_LE(heap.at(i).val, heap.at(left).val);
            }
            if (right < heap.size()) {
                EXPECT_LE(heap.at(i).val, heap.at(right).val);
            }
        }
    }
    // Массовые вставка и удаление
    TEST_F(HeapTest, MassPushAndErase) {
        const int NUM_ITEMS = 1000;
        std::vector<uint64_t> values;
        // используем локальный вектор для хранения индексов, чтобы избежать переполнения массива indices
        std::vector<size_t> local_indices(NUM_ITEMS);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10000);
        for (int i = 0; i < NUM_ITEMS; ++i) {
            uint64_t val = dis(gen);
            values.push_back(val);
            heap.push(val, &local_indices[i]);
        }
        EXPECT_EQ(heap.size(), NUM_ITEMS);
        uint64_t prev = 0;
        int count = 0;
        while (!heap.empty()) {
            uint64_t current = heap.top().val;
            EXPECT_GE(current, prev);
            prev = current;
            heap.erase(0);
            count++;
        }
        EXPECT_EQ(count, NUM_ITEMS);
    }
    // Изменение приоритета
    TEST_F(HeapTest, PriorityChange) {
        heap.push(100, &indices[0]);
        heap.push(200, &indices[1]);
        heap.push(300, &indices[2]);
        heap.update(2, 50);
        EXPECT_EQ(heap.top().val, 50);
        EXPECT_EQ(heap.top().ref, &indices[2]);
        heap.update(0, 1000);
        EXPECT_EQ(heap.top().val, 100);
        std::vector<uint64_t> expected = {100, 200, 1000};
        std::vector<uint64_t> actual;
        while (!heap.empty()) {
            actual.push_back(heap.top().val);
            heap.erase(0);
        }
        EXPECT_EQ(actual, expected);
    }
    // Проверка итераторов
    TEST_F(HeapTest, Iterators) {
        heap.push(100, &indices[0]);
        heap.push(200, &indices[1]);
        heap.push(300, &indices[2]);
        std::vector<uint64_t> values;
        for (const auto& item : heap) {
            values.push_back(item.val);
        }
        EXPECT_EQ(values.size(), 3);
        std::sort(values.begin(), values.end());
        std::vector<uint64_t> expected = {100, 200, 300};
        EXPECT_EQ(values, expected);
    }
    TEST_F(HeapTest, ConstIterators) {
        heap.push(100, &indices[0]);

        const Heap& const_heap = heap;
        auto it = const_heap.begin();
        EXPECT_EQ(it->val, 100);
        EXPECT_EQ(it->ref, &indices[0]);

        auto end = const_heap.end();
        EXPECT_NE(it, end);

        ++it;
        EXPECT_EQ(it, end);
    }
    // Операторы сравнения
    TEST_F(HeapTest, ComparisonOperators) {
        HeapItem item1(100, &indices[0]);
        HeapItem item2(200, &indices[1]);
        HeapItem item3(100, &indices[2]);
        EXPECT_TRUE(item1 < item2);
        EXPECT_FALSE(item1 > item2);
        EXPECT_TRUE(item1 <= item3);
        EXPECT_TRUE(item1 >= item3);
        EXPECT_TRUE(item1 == item3);
        EXPECT_FALSE(item1 == item2);
    }
    // Семантика перемещения
    TEST_F(HeapTest, MoveSemantics) {
        heap.push(100, &indices[0]);
        heap.push(200, &indices[1]);
        heap.push(300, &indices[2]);
        Heap moved_heap = std::move(heap);
        EXPECT_EQ(moved_heap.size(), 3);
        EXPECT_EQ(moved_heap.top().val, 100);
        EXPECT_EQ(heap.size(), 0);
        EXPECT_TRUE(heap.empty());
        for (size_t i = 0; i < moved_heap.size(); ++i) {
            EXPECT_EQ(*moved_heap.at(i).ref, i);
        }
    }
    // Множественные операции
    TEST_F(HeapTest, MultipleOperations) {
        for (int i = 0; i < 100; ++i) {
            heap.push(i * 10, &indices[i]);
        }
        EXPECT_EQ(heap.size(), 100);
        EXPECT_EQ(heap.top().val, 0);
        heap.update(50, 5);
        EXPECT_EQ(heap.top().val, 0);
        heap.update(0, 1000);
        EXPECT_EQ(heap.top().val, 5);
        for (int i = 0; i < 100; i += 2) {
            size_t pos = 0;
            for (size_t j = 0; j < heap.size(); ++j) {
                if (heap.at(j).val == static_cast<uint64_t>(i * 10)) {
                    pos = j;
                    break;
                }
            }
            if (pos < heap.size()) {
                heap.erase(pos);
            }
        }
        for (size_t i = 0; i < heap.size(); ++i) {
            size_t left = 2 * i + 1;
            size_t right = 2 * i + 2;
            if (left < heap.size()) {
                EXPECT_LE(heap.at(i).val, heap.at(left).val);
            }
            if (right < heap.size()) {
                EXPECT_LE(heap.at(i).val, heap.at(right).val);
            }
        }
    }
}