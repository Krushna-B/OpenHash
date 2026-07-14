#include <cstddef>
#include <mutex>
#include <shared_mutex>

// Mutex locked store wrapper
template <typename Store> class LockedStore {
  Store store;
  mutable std::shared_mutex mutex;

public:
  explicit LockedStore(size_t n) : store(n) {}

  void set(const std::string &key, const std::string &value) {
    std::unique_lock lock(mutex);
    store.set(key, value);
  }

  bool get(const std::string &key, std::string &out) const {
    std::shared_lock lock(mutex);
    return store.get(key, out);
  }

  bool del(const std::string &key) {
    std::unique_lock lock(mutex);
    return store.del(key);
  }
  size_t size() const {
    std::shared_lock lock(mutex);
    return store.size();
  };
};
