#include <iostream>
#include <string>

#include "kv_store.hpp"

// assert() is disabled in Release builds (NDEBUG), so use our own check
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAILED: " << #cond << " (line " << __LINE__ << ")\n";      \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main() {
  KVStore store;
  std::string out;

  // set/get roundtrip
  store.set("dog", "woof");
  CHECK(store.get("dog", out));
  CHECK(out == "woof");

  // missing key
  CHECK(!store.get("cat", out));

  // overwrite, not duplicate
  store.set("dog", "bark");
  CHECK(store.get("dog", out));
  CHECK(out == "bark");
  CHECK(store.size() == 1);

  // del
  CHECK(store.del("dog"));
  CHECK(!store.get("dog", out));
  CHECK(store.size() == 0);

  // del on missing key
  CHECK(!store.del("dog"));

  // insert enough keys to force multiple resizes, everything stays retrievable
  const int n = 1000;
  for (int i = 0; i < n; i++) {
    store.set("key" + std::to_string(i), "val" + std::to_string(i));
  }
  CHECK(store.size() == n);
  for (int i = 0; i < n; i++) {
    CHECK(store.get("key" + std::to_string(i), out));
    CHECK(out == "val" + std::to_string(i));
  }

  std::cout << "all tests passed\n";
  return 0;
}
