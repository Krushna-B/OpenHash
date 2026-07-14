#include <gtest/gtest.h>
#include <string>

#include "kv_store.hpp"

TEST(KVStoreTest, SetGetRoundtrip) {
  KVStore store(16);
  std::string out;

  store.set("dog", "woof");
  EXPECT_TRUE(store.get("dog", out));
  EXPECT_EQ(out, "woof");
}

TEST(KVStoreTest, MissingKey) {
  KVStore store(16);
  std::string out;

  EXPECT_FALSE(store.get("cat", out));
}

TEST(KVStoreTest, OverwriteNotDuplicate) {
  KVStore store(16);
  std::string out;

  store.set("dog", "woof");
  store.set("dog", "bark");
  EXPECT_TRUE(store.get("dog", out));
  EXPECT_EQ(out, "bark");
  EXPECT_EQ(store.size(), 1u);
}

TEST(KVStoreTest, Delete) {
  KVStore store(16);
  std::string out;

  store.set("dog", "woof");
  EXPECT_TRUE(store.del("dog"));
  EXPECT_FALSE(store.get("dog", out));
  EXPECT_EQ(store.size(), 0u);
}

TEST(KVStoreTest, DeleteMissing) {
  KVStore store(16);

  EXPECT_FALSE(store.del("dog"));
}

TEST(KVStoreTest, SurvivesResize) {
  KVStore store(16);
  std::string out;

  // enough keys to force multiple resizes
  const int n = 1000;
  for (int i = 0; i < n; i++) {
    store.set("key" + std::to_string(i), "val" + std::to_string(i));
  }
  EXPECT_EQ(store.size(), static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    ASSERT_TRUE(store.get("key" + std::to_string(i), out));
    EXPECT_EQ(out, "val" + std::to_string(i));
  }
}
