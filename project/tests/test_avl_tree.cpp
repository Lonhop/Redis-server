#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
#include <random>
#include <set>

#include "data_structures/avl_tree.h"

namespace redis::data_structures {
    struct IntNode : public AVLNode {
        int key;
        IntNode(int k) : key(k) { init(); }
    };
    struct StringNode : public AVLNode {
        std::string key;
        StringNode(std::string k) : key(std::move(k)) { init(); }
    };
    struct IntNodeComparator {
        bool operator()(const AVLNode* a, const AVLNode* b) const {
            return static_cast<const IntNode*>(a)->key < static_cast<const IntNode*>(b)->key;
        }
    };
    struct IntKeyComparator {
        bool operator()(int key, const AVLNode* node) const {
            return key < static_cast<const IntNode*>(node)->key;
        }
        bool operator()(const AVLNode* node, int key) const {
            return static_cast<const IntNode*>(node)->key < key;
        }
    };
    struct StringNodeComparator {
        bool operator()(const AVLNode* a, const AVLNode* b) const {
            return static_cast<const StringNode*>(a)->key < static_cast<const StringNode*>(b)->key;
        }
    };
    struct StringKeyComparator {
        bool operator()(const std::string& key, const AVLNode* node) const {
            return key < static_cast<const StringNode*>(node)->key;
        }
        bool operator()(const AVLNode* node, const std::string& key) const {
            return static_cast<const StringNode*>(node)->key < key;
        }
    };
    class AVLTreeTest : public ::testing::Test {
    protected:
        AVLTree tree{IntNodeComparator{}};
        std::vector<IntNode*> created_nodes;
        IntNode* make_node(int key) {
            auto* n = new IntNode(key);
            created_nodes.push_back(n);
            return n;
        }
        void TearDown() override {
            for (auto* n : created_nodes) delete n;
        }
        std::vector<int> get_inorder_keys() {
            std::vector<int> result;
            std::function<void(AVLNode*)> traverse = [&](AVLNode* n) {
                if (!n) return;
                traverse(n->left);
                result.push_back(static_cast<IntNode*>(n)->key);
                traverse(n->right);
            };
            traverse(tree.root());
            return result;
        }
    };
    TEST_F(AVLTreeTest, BasicInsertAndOrder) {
        std::vector<int> keys = {50, 30, 70, 20, 40, 60, 80};
        for (int k : keys) tree.insert(make_node(k));
        EXPECT_EQ(tree.size(), 7);
        auto ordered = get_inorder_keys();
        std::vector<int> expected = {20, 30, 40, 50, 60, 70, 80};
        EXPECT_EQ(ordered, expected);
    }
    TEST_F(AVLTreeTest, SearchTest) {
        tree.insert(make_node(10));
        tree.insert(make_node(20));
        IntKeyComparator cmp;
        EXPECT_NE(tree.find(10, cmp), nullptr);
        EXPECT_NE(tree.find(20, cmp), nullptr);
        EXPECT_EQ(tree.find(30, cmp), nullptr);
    }
    TEST_F(AVLTreeTest, StringKeyTree) {
        AVLTree s_tree{StringNodeComparator{}};
        std::vector<StringNode*> s_nodes;
        auto make_s_node = [&](std::string k) {
            auto* n = new StringNode(std::move(k));
            s_nodes.push_back(n);
            return n;
        };
        s_tree.insert(make_s_node("apple"));
        s_tree.insert(make_s_node("cherry"));
        s_tree.insert(make_s_node("banana"));
        StringKeyComparator s_cmp;
        AVLNode* found = s_tree.find("banana", s_cmp);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(static_cast<StringNode*>(found)->key, "banana");
        for (auto* n : s_nodes) delete n;
    }
    TEST_F(AVLTreeTest, DeletionAndBalance) {
        std::vector<int> keys = {10, 20, 30, 40, 50};
        for (int k : keys) tree.insert(make_node(k));
        IntKeyComparator cmp;
        AVLNode* n30 = tree.find(30, cmp);
        tree.remove(n30);
        EXPECT_EQ(tree.size(), 4);
        EXPECT_EQ(tree.find(30, cmp), nullptr);
        auto ordered = get_inorder_keys();
        std::vector<int> expected = {10, 20, 40, 50};
        EXPECT_EQ(ordered, expected);
    }
    TEST_F(AVLTreeTest, RankAndOffset) {
        std::vector<int> keys = {10, 20, 30, 40, 50};
        for (int k : keys) tree.insert(make_node(k));
        IntKeyComparator cmp;
        AVLNode* n30 = tree.find(30, cmp);
        EXPECT_EQ(tree.rank(n30), 2);
        AVLNode* n40 = tree.offset(n30, 1);
        ASSERT_NE(n40, nullptr);
        EXPECT_EQ(static_cast<IntNode*>(n40)->key, 40);
        AVLNode* n10 = tree.offset(n30, -2);
        ASSERT_NE(n10, nullptr);
        EXPECT_EQ(static_cast<IntNode*>(n10)->key, 10);
    }
    TEST_F(AVLTreeTest, LargeScaleStability) {
        const int count = 1000;
        std::vector<int> keys;
        for(int i=0; i<count; ++i) keys.push_back(i);
        auto rng = std::default_random_engine {};
        std::shuffle(keys.begin(), keys.end(), rng);
        for(int k : keys) tree.insert(make_node(k));
        EXPECT_EQ(tree.size(), count);
        #ifndef NDEBUG
            EXPECT_NO_THROW(tree.check_invariants());
        #endif
        for(int i=0; i < count / 2; ++i) {
            IntKeyComparator cmp;
            AVLNode* n = tree.find(keys[i], cmp);
            tree.remove(n);
        }
        EXPECT_EQ(tree.size(), count / 2);
    }
}