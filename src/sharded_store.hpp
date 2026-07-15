#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
template <typename Store, size_t NUM_SHARDS = 16> class ShardedStore {

  // Create a shard of size n
  // The alignas 64 forces allocation on 64byte boundary, cache line's are 64
  // bytes typically on ARM
  // TODO: Test this with other byte sizes to see if it makes a difference and
  // by how much
  struct alignas(128) Shard {
    Store store;
    mutable std::shared_mutex mutex;
    explicit Shard(size_t n) : store(n) {}
  };
  std::vector<std::unique_ptr<Shard>> shards;
  size_t shard_for(const std::string &key) const {
    size_t h = std::hash<std::string>{}(key);
    return (h >> 32) % NUM_SHARDS;
  }

public:
  explicit ShardedStore(size_t total_slots) {
    shards.reserve(NUM_SHARDS);
    for (size_t s{}; s < NUM_SHARDS; ++s) {
      shards.push_back(std::make_unique<Shard>(total_slots / NUM_SHARDS + 1));
    }
  }
  void set(const std::string &key, const std::string &value) {
    Shard &sh = *shards[shard_for(key)];
    std::unique_lock lock(sh.mutex);
    sh.store.set(key, value);
  }

  bool get(const std::string &key, std::string &out) const {
    Shard &sh = *shards[shard_for(key)];
    std::shared_lock lock(sh.mutex);
    return sh.store.get(key, out);
  }

  bool del(const std::string &key) {
    Shard &sh = *shards[shard_for(key)];
    std::unique_lock lock(sh.mutex);
    return sh.store.del(key);
  }

  size_t size() const {
    size_t sum{};
    for (auto &sh : shards) {
      std::shared_lock lock(sh.mutex);
      sum += sh.store.size;
    }
    return sum;
  };
};