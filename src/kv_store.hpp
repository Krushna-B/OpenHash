#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Hold our <Key, Value> Pairs
struct Entry {
  std::string key;
  std::string value;
};

struct Bucket {
  std::vector<Entry> entries;
};

class KVStore {
private:
  std::vector<Bucket> buckets;
  size_t _size{};

  size_t index_for(const std::string &key) const {
    return std::hash<std::string>{}(key) % buckets.size();
  }

public:
  explicit KVStore(size_t bucket_count) : buckets(bucket_count) {}
  void set(const std::string &key, const std::string &value);
  bool get(const std::string &key, std::string &out) const;
  bool del(const std::string &key);
  size_t size() const;
};
