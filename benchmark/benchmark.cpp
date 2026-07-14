#include "kv_store.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

const int SEED = 42;
const int NUM_KEYS = 1'000'000;
const int NUM_GETS = 10'000'000;
const size_t STRING_LENGTH = 16;
const std::string charset =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

double seconds_since(std::chrono::steady_clock::time_point start) {
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(end - start).count();
}

std::vector<std::string> generate_random_strings(int n) {
  std::vector<std::string> random_strings;
  random_strings.reserve(n);

  // C++ random engine
  std::mt19937 generator(SEED); // fixed seed: same keys every run
  std::uniform_int_distribution<size_t> distribution(0, charset.size() - 1);

  for (size_t i{}; i < static_cast<size_t>(n); ++i) {
    std::string str(STRING_LENGTH, ' ');
    for (size_t j = 0; j < STRING_LENGTH; ++j) {
      str[j] = charset[distribution(generator)];
    }
    random_strings.push_back(std::move(str));
  }
  return random_strings;
}

std::vector<uint32_t> generate_random_indices(int n, int max) {
  std::vector<uint32_t> indices;
  indices.reserve(n);

  std::mt19937 generator(1337);
  std::uniform_int_distribution<uint32_t> distribution(0, max - 1);

  for (int i = 0; i < n; ++i) {
    indices.push_back(distribution(generator));
  }
  return indices;
}

int main() {
  // Setup: all randomness generated before any clock starts
  auto start = std::chrono::steady_clock::now();
  std::vector<std::string> keys = generate_random_strings(NUM_KEYS);
  std::vector<uint32_t> lookup_order =
      generate_random_indices(NUM_GETS, NUM_KEYS);
  std::cout << "setup: generated " << NUM_KEYS << " keys in "
            << seconds_since(start) << " s\n\n";

  {
    KVStore store(2 << 18);

    start = std::chrono::steady_clock::now();
    for (const auto &key : keys) {
      store.set(key, key);
    }
    double insert_s = seconds_since(start);

    std::string out;
    size_t found = 0;
    start = std::chrono::steady_clock::now();
    for (uint32_t idx : lookup_order) {
      if (store.get(keys[idx], out)) {
        found++;
      }
    }
    double get_s = seconds_since(start);

    std::cout << "KVStore\n";
    std::cout << "  insert " << NUM_KEYS << ": " << insert_s << " s  ("
              << NUM_KEYS / insert_s / 1e6 << " Mops/s)\n";
    std::cout << "  get    " << NUM_GETS << ": " << get_s << " s  ("
              << NUM_GETS / get_s / 1e6 << " Mops/s)  [found " << found
              << "]\n\n";
  }

  // std::unordered_map baseline
  {
    std::unordered_map<std::string, std::string> map;

    start = std::chrono::steady_clock::now();
    for (const auto &key : keys) {
      map[key] = key;
    }
    double insert_s = seconds_since(start);

    size_t found = 0;
    start = std::chrono::steady_clock::now();
    for (uint32_t idx : lookup_order) {
      auto it = map.find(keys[idx]);
      if (it != map.end()) {
        found++;
      }
    }
    double get_s = seconds_since(start);

    std::cout << "std::unordered_map\n";
    std::cout << "  insert " << NUM_KEYS << ": " << insert_s << " s  ("
              << NUM_KEYS / insert_s / 1e6 << " Mops/s)\n";
    std::cout << "  get    " << NUM_GETS << ": " << get_s << " s  ("
              << NUM_GETS / get_s / 1e6 << " Mops/s)  [found " << found
              << "]\n";
  }

  return 0;
}
