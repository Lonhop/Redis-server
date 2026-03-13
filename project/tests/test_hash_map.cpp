#include <gtest/gtest.h>
#include "data_structures/hash_map.h"
#include <string>
#include <vector>
#include <memory>

namespace redis::data_structures {
    struct TestEntry {
        HNode node;
        std::string key;
        std::string value;
        TestEntry(std::string k, std::string v, uint32_t h) : key(std::move(k)), value(std::move(v)) {
            node.next = nullptr;
            node.hcode = h;
        }
    };
    uint64_t mock_hash(const void* data, size_t len) { return 0; }
    bool mock_eq(const HNode* a, const HNode* b) {
        auto* entry_a = reinterpret_cast<const TestEntry*>(a);
        auto* entry_b = reinterpret_cast<const TestEntry*>(b);
        return entry_a->key == entry_b->key;
    }
    class HashMapTest : public ::testing::Test {
    protected:
        using TestMap = HashMap<decltype(mock_eq)*, decltype(mock_hash)*>;
    };
    TEST_F(HashMapTest, BasicOperations) {
        TestMap table(mock_hash, mock_eq, 4);
        TestEntry e1("key1", "val1", 101);
        table.insert(&e1.node);
        HNode* found = table.lookup(&e1.node);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(reinterpret_cast<TestEntry*>(found)->value, "val1");
        HNode* popped = table.pop(&e1.node);
        EXPECT_EQ(popped, &e1.node);
        EXPECT_EQ(table.size(), 0);
    }
    TEST_F(HashMapTest, CollisionHandling) {
        TestMap table(mock_hash, mock_eq, 8);
        TestEntry e1("key1", "val1", 50);
        TestEntry e2("key2", "val2", 50);
        table.insert(&e1.node);
        table.insert(&e2.node);
        EXPECT_EQ(table.size(), 2);
        EXPECT_NE(table.lookup(&e1.node), table.lookup(&e2.node));
    }
    TEST_F(HashMapTest, IncrementalResizing) {
        TestMap table(mock_hash, mock_eq, 4);
        std::vector<std::unique_ptr<TestEntry>> entries;
        for (int i = 0; i < 10; ++i) {
            entries.push_back(std::make_unique<TestEntry>(
                "key_" + std::to_string(i),
                "val_" + std::to_string(i),
                static_cast<uint32_t>(i)
            ));
            table.insert(&entries.back()->node);
        }
        EXPECT_EQ(table.size(), 10);
        for (int i = 0; i < 10; ++i) {
            TestEntry search_key("key_" + std::to_string(i), "", i);
            HNode* found = table.lookup(&search_key.node);
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(reinterpret_cast<TestEntry*>(found)->value, "val_" + std::to_string(i));
        }
    }
}