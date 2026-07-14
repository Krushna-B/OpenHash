#include "kv_store.hpp"
#include "kv_store_arena.hpp"
#include "kv_store_open.hpp"
#include "gtest/gtest.h"
#include <gtest/gtest.h>
#include <string>

template <typename T> class KVStoreTest : public testing::Test {};
using StoreTypes = testing::Types<KVStore, KVStoreOpen, KVStoreArena>;
TYPED_TEST_SUITE(KVStoreTest, StoreTypes);

TYPED_TEST(KVStoreTest, SetGetRoundtrip) {
  TypeParam store(16);
  std::string out;

  store.set("dog", "woof");
  EXPECT_TRUE(store.get("dog", out));
  EXPECT_EQ(out, "woof");
}

TYPED_TEST(KVStoreTest, MissingKey) {
  TypeParam store(16);
  std::string out;

  EXPECT_FALSE(store.get("cat", out));
}

TYPED_TEST(KVStoreTest, OverwriteNotDuplicate) {
  TypeParam store(16);
  std::string out;

  store.set("dog", "woof");
  store.set("dog", "bark");
  EXPECT_TRUE(store.get("dog", out));
  EXPECT_EQ(out, "bark");
  EXPECT_EQ(store.size(), 1u);
}

TYPED_TEST(KVStoreTest, Delete) {
  TypeParam store(16);
  std::string out;

  store.set("dog", "woof");
  EXPECT_TRUE(store.del("dog"));
  EXPECT_FALSE(store.get("dog", out));
  EXPECT_EQ(store.size(), 0u);
}

TYPED_TEST(KVStoreTest, DeleteMissing) {
  TypeParam store(16);

  EXPECT_FALSE(store.del("dog"));
}

TYPED_TEST(KVStoreTest, SurvivesResize) {
  TypeParam store(16);
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
