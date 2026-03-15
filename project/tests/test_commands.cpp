#include <gtest/gtest.h>

#include "core/commands.h"

namespace redis::core::test {

// make sure basic reading and writing actually works
TEST(CommandsTest, SetAndGet) {
    KeyValueStore store;

    // put something in
    auto set_command = create_command({"SET", "name", "alice"});
    EXPECT_EQ(set_command->execute(store), "+OK\r\n");

    // get it back out
    auto get_command = create_command({"GET", "name"});
    EXPECT_EQ(get_command->execute(store), "$5\r\nalice\r\n");
}

// ask for something we never saved
TEST(CommandsTest, GetMissingKey) {
    KeyValueStore store;

    auto get_command = create_command({"GET", "missing"});
    EXPECT_EQ(get_command->execute(store), "$-1\r\n"); // null bulk string
}

// make sure we can actually throw things away
TEST(CommandsTest, DelSingleKey) {
    KeyValueStore store;

    EXPECT_EQ(create_command({"SET", "a", "1"})->execute(store), "+OK\r\n");
    EXPECT_EQ(create_command({"DEL", "a"})->execute(store), ":1\r\n"); // 1 item deleted
    EXPECT_EQ(create_command({"GET", "a"})->execute(store), "$-1\r\n"); // should be gone now
}

// test deleting a bunch of stuff at once, including stuff that isnt there
TEST(CommandsTest, DelMultipleKeys) {
    KeyValueStore store;

    EXPECT_EQ(create_command({"SET", "a", "1"})->execute(store), "+OK\r\n");
    EXPECT_EQ(create_command({"SET", "b", "2"})->execute(store), "+OK\r\n");
    // c doesnt exist, so it should only report deleting 2 items
    EXPECT_EQ(create_command({"DEL", "a", "b", "c"})->execute(store), ":2\r\n");
}

// test that writing to the same key just overwrites the old garbage
TEST(CommandsTest, SetOverridesValue) {
    KeyValueStore store;

    EXPECT_EQ(create_command({"SET", "k", "v1"})->execute(store), "+OK\r\n");
    EXPECT_EQ(create_command({"SET", "k", "v2"})->execute(store), "+OK\r\n");
    EXPECT_EQ(create_command({"GET", "k"})->execute(store), "$2\r\nv2\r\n");
}

// test what happens when users inevitably type things wrong
TEST(CommandsTest, WrongArityReturnsError) {
    KeyValueStore store;

    EXPECT_EQ(create_command({"SET", "only_key"})->execute(store), "-ERR wrong number of arguments for SET\r\n");
    EXPECT_EQ(create_command({"GET"})->execute(store), "-ERR wrong number of arguments for GET\r\n");
    EXPECT_EQ(create_command({"DEL"})->execute(store), "-ERR wrong number of arguments for DEL\r\n");
}

// test when someone types a command we havent built yet
TEST(CommandsTest, UnknownCommandReturnsError) {
    KeyValueStore store;

    EXPECT_EQ(create_command({"PING"})->execute(store), "-ERR unknown command\r\n");
}

}