#include <gtest/gtest.h>
#include <gtest/gtest-death-test.h>
#include <algorithm>
#include <vector>
#include <random>
#include <set>
#include <thread>

#include "data_structures/avl_tree.h"

namespace redis::data_structures::test {
    struct TestNode : public AVLNode {
        int key;
        TestNode(int k) : key(k) {
            init();
        }
    };

    struct TestNodeWithString : public AVLNode {
        std::string key;
        std::string value;
        TestNodeWithString(const std::string& k, const std::string& v = "") : key(k), value(v) {
            init();
        }
    };

    // Компаратор для поиска по ключу типа int
    struct IntKeyComparator {
        bool operator()(const AVLNode* node, int key) const {
            return static_cast<const TestNode*>(node)->key < key;
        }
        bool operator()(int key, const AVLNode* node) const {
            return key < static_cast<const TestNode*>(node)->key;
        }
    };

    // Компаратор для поиска по ключу типа string
    struct StringKeyComparator {
        bool operator()(const AVLNode* node, const std::string& key) const {
            return static_cast<const TestNodeWithString*>(node)->key < key;
        }
        bool operator()(const std::string& key, const AVLNode* node) const {
            return key < static_cast<const TestNodeWithString*>(node)->key;
        }
    };

    // Компаратор для сравнения узлов типа int
    struct IntNodeComparator {
        bool operator()(const AVLNode* a, const AVLNode* b) const {
            return static_cast<const TestNode*>(a)->key < static_cast<const TestNode*>(b)->key;
        }
    };

    // Компаратор для сравнения узлов типа string
    struct StringNodeComparator {
        bool operator()(const AVLNode* a, const AVLNode* b) const {
            return static_cast<const TestNodeWithString*>(a)->key < static_cast<const TestNodeWithString*>(b)->key;
        }
    };

    std::vector<int> collect_keys(const AVLTree& tree) {
        std::vector<int> result;
        std::function<void(AVLNode*)> traverse = [&](AVLNode* node) {
            if (!node) return;
            traverse(node->left);
            result.push_back(static_cast<TestNode*>(node)->key);
            traverse(node->right);
        };
        traverse(tree.root());
        return result;
    }

    std::vector<std::string> collect_string_keys(const AVLTree& tree) {
        std::vector<std::string> result;
        std::function<void(AVLNode*)> traverse = [&](AVLNode* node) {
            if (!node) return;
            traverse(node->left);
            result.push_back(static_cast<TestNodeWithString*>(node)->key);
            traverse(node->right);
        };
        traverse(tree.root());
        return result;
    }

    bool is_sorted(const std::vector<int>& vec) {
        for (size_t i = 1; i < vec.size(); ++i) {
            if (vec[i-1] > vec[i]) return false;
        }
        return true;
    }

    // Класс для тестов
    class AVLTreeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            nodes.clear();
            // Важно: создаем дерево с компаратором для int!
            tree = AVLTree(IntNodeComparator{});
        }

        void TearDown() override {
            for (auto* node : nodes) {
                delete node;
            }
        }

        TestNode* create_node(int key) {
            auto* node = new TestNode(key);
            nodes.push_back(node);
            return node;
        }

        TestNodeWithString* create_node(const std::string& key, const std::string& val = "") {
            auto* node = new TestNodeWithString(key, val);
            nodes.push_back(node);
            return node;
        }

        AVLTree tree;
        std::vector<AVLNode*> nodes;
    };

    // Тесты
    TEST_F(AVLTreeTest, BasicInsertAndSize) {
        EXPECT_TRUE(tree.empty());
        EXPECT_EQ(tree.size(), 0);
        for (int i = 0; i < 10; ++i) {
            tree.insert(create_node(i));
            EXPECT_EQ(tree.size(), i + 1);
        }
        EXPECT_FALSE(tree.empty());
        EXPECT_EQ(tree.size(), 10);
    }

    TEST_F(AVLTreeTest, DuplicateInsert) {
        TestNode* node1 = create_node(5);
        TestNode* node2 = create_node(5);

        EXPECT_NO_THROW(tree.insert(node1));
        EXPECT_EQ(tree.size(), 1);
        EXPECT_TRUE(node1->in_tree());

        EXPECT_THROW(tree.insert(node1), std::runtime_error);
        EXPECT_EQ(tree.size(), 1);

        EXPECT_NO_THROW(tree.insert(node2));
        EXPECT_EQ(tree.size(), 2);

        EXPECT_TRUE(node1->in_tree());
        EXPECT_TRUE(node2->in_tree());
    }

    TEST_F(AVLTreeTest, FindElements) {
        std::vector<int> keys = {5, 3, 7, 2, 4, 6, 8};
        for (int k : keys) {
            tree.insert(create_node(k));
        }
        IntKeyComparator cmp;
        for (int k : keys) {
            AVLNode* found = tree.find(k, cmp);
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(static_cast<TestNode*>(found)->key, k);
        }
        EXPECT_EQ(tree.find(100, cmp), nullptr);
        EXPECT_EQ(tree.find(-5, cmp), nullptr);
    }

    TEST_F(AVLTreeTest, FindStringKeys) {
        std::vector<std::string> keys = {"apple", "banana", "cherry", "date", "fig"};
        for (const auto& k : keys) {
            tree.insert(create_node(k));
        }
        StringKeyComparator cmp;
        for (const auto& k : keys) {
            AVLNode* found = tree.find(k, cmp);
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(static_cast<TestNodeWithString*>(found)->key, k);
        }
        EXPECT_EQ(tree.find("grape", cmp), nullptr);
    }

    TEST_F(AVLTreeTest, InOrderTraversal) {
        std::vector<int> keys = {5, 3, 8, 1, 4, 7, 9, 2, 6};
        for (int k : keys) {
            tree.insert(create_node(k));
        }
        auto collected = collect_keys(tree);
        EXPECT_TRUE(is_sorted(collected));
        std::sort(keys.begin(), keys.end());
        EXPECT_EQ(collected, keys);
    }

    TEST_F(AVLTreeTest, RemoveLeaf) {
        tree.insert(create_node(10));
        tree.insert(create_node(5));
        tree.insert(create_node(15));
        EXPECT_EQ(tree.size(), 3);
        IntKeyComparator cmp;
        AVLNode* node5 = tree.find(5, cmp);
        ASSERT_NE(node5, nullptr);
        AVLNode* removed = tree.remove(node5);
        EXPECT_EQ(removed, node5);
        EXPECT_EQ(tree.size(), 2);
        auto collected = collect_keys(tree);
        std::vector<int> expected = {10, 15};
        EXPECT_EQ(collected, expected);
        EXPECT_EQ(node5->left, nullptr);
        EXPECT_EQ(node5->right, nullptr);
        EXPECT_EQ(node5->parent, nullptr);
    }

    TEST_F(AVLTreeTest, RemoveNodeWithOneChild) {
        tree.insert(create_node(10));
        tree.insert(create_node(5));
        tree.insert(create_node(15));
        tree.insert(create_node(3));
        EXPECT_EQ(tree.size(), 4);
        IntKeyComparator cmp;
        AVLNode* node5 = tree.find(5, cmp);
        ASSERT_NE(node5, nullptr);
        tree.remove(node5);
        EXPECT_EQ(tree.size(), 3);
        auto collected = collect_keys(tree);
        std::vector<int> expected = {3, 10, 15};
        EXPECT_EQ(collected, expected);
    }

    TEST_F(AVLTreeTest, RemoveNodeWithTwoChildren) {
        tree.insert(create_node(20));
        tree.insert(create_node(10));
        tree.insert(create_node(30));
        tree.insert(create_node(5));
        tree.insert(create_node(15));
        tree.insert(create_node(25));
        tree.insert(create_node(35));
        EXPECT_EQ(tree.size(), 7);
        IntKeyComparator cmp;
        AVLNode* node20 = tree.find(20, cmp);
        ASSERT_NE(node20, nullptr);
        tree.remove(node20);
        EXPECT_EQ(tree.size(), 6);
        auto collected = collect_keys(tree);
        std::vector<int> expected = {5, 10, 15, 25, 30, 35};
        EXPECT_EQ(collected, expected);
    }

    TEST_F(AVLTreeTest, Rank) {
        std::vector<int> keys = {50, 30, 70, 20, 40, 60, 80};
        for (int k : keys) {
            tree.insert(create_node(k));
        }
        IntKeyComparator cmp;
        EXPECT_EQ(tree.rank(tree.find(20, cmp)), 0);
        EXPECT_EQ(tree.rank(tree.find(30, cmp)), 1);
        EXPECT_EQ(tree.rank(tree.find(40, cmp)), 2);
        EXPECT_EQ(tree.rank(tree.find(50, cmp)), 3);
        EXPECT_EQ(tree.rank(tree.find(60, cmp)), 4);
        EXPECT_EQ(tree.rank(tree.find(70, cmp)), 5);
        EXPECT_EQ(tree.rank(tree.find(80, cmp)), 6);
    }

    TEST_F(AVLTreeTest, Offset) {
        std::vector<int> keys = {10, 20, 30, 40, 50, 60, 70, 80, 90};
        for (int k : keys) {
            tree.insert(create_node(k));
        }
        IntKeyComparator cmp;
        AVLNode* node50 = tree.find(50, cmp);
        ASSERT_NE(node50, nullptr);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, 1))->key, 60);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, 2))->key, 70);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, 3))->key, 80);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, 4))->key, 90);
        EXPECT_EQ(tree.offset(node50, 5), nullptr);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, -1))->key, 40);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, -2))->key, 30);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, -3))->key, 20);
        EXPECT_EQ(static_cast<TestNode*>(tree.offset(node50, -4))->key, 10);
        EXPECT_EQ(tree.offset(node50, -5), nullptr);
    }

    TEST_F(AVLTreeTest, Count) {
        std::vector<int> keys = {15, 5, 20, 3, 10, 17, 25, 1, 7, 12, 22, 30};
        for (int k : keys) {
            tree.insert(create_node(k));
        }
        IntKeyComparator cmp;
        AVLNode* min = tree.find(10, cmp);
        AVLNode* max = tree.find(22, cmp);
        ASSERT_NE(min, nullptr);
        ASSERT_NE(max, nullptr);
        EXPECT_EQ(tree.count(min, max), 6);
    }

    TEST_F(AVLTreeTest, BalanceAfterManyInserts) {
        const int NUM_KEYS = 1000;
        for (int i = 0; i < NUM_KEYS; ++i) {
            tree.insert(create_node(i));
        }
        EXPECT_EQ(tree.size(), NUM_KEYS);
        auto collected = collect_keys(tree);
        EXPECT_TRUE(is_sorted(collected));
        std::function<int(AVLNode*)> get_height = [&](AVLNode* node) -> int {
            if (!node) return 0;
            return 1 + std::max(get_height(node->left), get_height(node->right));
        };
        int height = get_height(tree.root());
        EXPECT_LT(height, 30);
    }

    TEST_F(AVLTreeTest, BalanceAfterRemovals) {
        const int NUM_KEYS = 500;
        std::vector<TestNode*> created_nodes;
        for (int i = 0; i < NUM_KEYS; ++i) {
            auto* node = create_node(i);
            created_nodes.push_back(node);
            tree.insert(node);
        }
        for (int i = 0; i < NUM_KEYS; i += 2) {
            tree.remove(created_nodes[i]);
        }
        EXPECT_EQ(tree.size(), NUM_KEYS / 2);
        auto collected = collect_keys(tree);
        EXPECT_TRUE(is_sorted(collected));
    }

    TEST_F(AVLTreeTest, RemoveAll) {
        std::vector<int> keys = {8, 3, 10, 1, 6, 14, 4, 7, 13};
        std::vector<TestNode*> created_nodes;
        for (int k : keys) {
            auto* node = create_node(k);
            created_nodes.push_back(node);
            tree.insert(node);
        }
        for (auto* node : created_nodes) {
            tree.remove(node);
        }
        EXPECT_TRUE(tree.empty());
        EXPECT_EQ(tree.size(), 0);
        EXPECT_EQ(tree.root(), nullptr);
    }

    TEST_F(AVLTreeTest, CompareWithStdSet) {
        const int NUM_KEYS = 500;
        std::set<int> std_set;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10000);
        for (int i = 0; i < NUM_KEYS; ++i) {
            int key = dis(gen);
            std_set.insert(key);
            tree.insert(create_node(key));
        }
        EXPECT_EQ(tree.size(), std_set.size());
        IntKeyComparator cmp;
        for (int key : std_set) {
            AVLNode* node = tree.find(key, cmp);
            EXPECT_NE(node, nullptr);
            EXPECT_EQ(static_cast<TestNode*>(node)->key, key);
        }
        auto collected = collect_keys(tree);
        EXPECT_TRUE(std::is_sorted(collected.begin(), collected.end()));
    }

    TEST_F(AVLTreeTest, ThreadSafety) {
        const int NUM_KEYS = 100;
        std::vector<TestNode*> nodes;
        for (int i = 0; i < NUM_KEYS; ++i) {
            nodes.push_back(create_node(i));
            tree.insert(nodes.back());
        }
        std::atomic<bool> stop{false};
        std::thread reader([&]() {
            IntKeyComparator cmp;
            while (!stop) {
                for (int i = 0; i < 10; ++i) {
                    tree.find(i, cmp);
                }
            }
        });
        std::thread writer([&]() {
            for (int i = 10; i < 20; ++i) {
                tree.remove(nodes[i]);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        writer.join();
        stop = true;
        reader.join();
        SUCCEED();
    }

    TEST_F(AVLTreeTest, InvalidArguments) {
        EXPECT_THROW(tree.insert(nullptr), std::invalid_argument);
        EXPECT_THROW(tree.remove(nullptr), std::invalid_argument);
        IntKeyComparator cmp;
        EXPECT_EQ(tree.find(10, cmp), nullptr);
    }

    TEST_F(AVLTreeTest, RemoveNodeNotInTree) {
        TestNode* node = create_node(42);
        EXPECT_THROW(tree.remove(node), std::runtime_error);
        tree.insert(node);
        EXPECT_NO_THROW(tree.remove(node));
        EXPECT_THROW(tree.remove(node), std::runtime_error);
    }

    TEST_F(AVLTreeTest, CheckInvariants) {
        EXPECT_NO_THROW(tree.check_invariants());

        for (int i = 0; i < 50; ++i) {
            tree.insert(create_node(i));
            EXPECT_NO_THROW(tree.check_invariants());
        }

        IntKeyComparator cmp;
        for (int i = 0; i < 50; i += 2) {
            AVLNode* node = tree.find(i, cmp);
            if (node) {
                tree.remove(node);
                EXPECT_NO_THROW(tree.check_invariants());
            }
        }
    }
}