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
  float _resize_threshold{};
  size_t _size{};

  size_t index_for(const std::string &key) const {
    return std::hash<std::string>{}(key) % buckets.size();
  }

  void resize() {
    std::vector<Bucket> new_buckets(buckets.size() * 2);
    for (auto &bucket : buckets) {
      for (auto &entry : bucket.entries) {
        size_t i = std::hash<std::string>{}(entry.key) % new_buckets.size();
        new_buckets[i].entries.push_back(std::move(entry));
      }
    }
    buckets = std::move(new_buckets);
  }

public:
  // Constructor
  explicit KVStore(size_t bucket_count, float _resize_threshold = 0.75)
      : buckets(bucket_count), _resize_threshold(_resize_threshold) {}

  void set(const std::string &key, const std::string &value) {
    size_t i = index_for(key);

    // Resize
    if ((_size + 1.0) / buckets.size() > _resize_threshold) {
      resize();
    }
    for (auto &entry : buckets[i].entries) {
      if (entry.key == key) {
        entry.value = value;
        return;
      }
    }
    buckets[i].entries.push_back(Entry{key, value});
    _size++;
  }

  bool get(const std::string &key, std::string &out) const {
    size_t i = index_for(key);
    for (auto &entry : buckets[i].entries) {
      if (entry.key == key) {
        out = entry.value;
        return true;
      }
    }
    return false;
  }

  bool del(const std::string &key) {
    size_t i = index_for(key);
    for (auto it = buckets[i].entries.begin(); it != buckets[i].entries.end();
         ++it) {
      if (it->key == key) {
        buckets[i].entries.erase(it);
        _size--;
        return true;
      }
    }
    return false;
  }
  size_t size() const { return _size; };
};
