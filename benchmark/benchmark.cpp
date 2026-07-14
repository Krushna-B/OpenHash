#include "kv_store.hpp"
#include "kv_store_open.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

const int SEED = 42;
const int NUM_KEYS = 1'000'000;
const int NUM_GETS = 10'000'000;
const int TRIALS = 5;
const size_t INITIAL_BUCKETS = 2 << 18;
const size_t STRING_LENGTH = 16;
const std::string charset =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

struct TrialResult {
  double insert_mops;
  double get_mops;
};

double seconds_since(std::chrono::steady_clock::time_point start) {
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(end - start).count();
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  size_t mid = v.size() / 2;
  return v.size() % 2 ? v[mid] : (v[mid - 1] + v[mid]) / 2;
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

// Run trials
template <typename TrialFn>
TrialResult run_bench(const std::string &name, TrialFn run_trial) {
  std::vector<double> insert_mops;
  std::vector<double> get_mops;

  std::cout << name << "\n";
  for (int t = 1; t <= TRIALS; ++t) {
    TrialResult r = run_trial();
    insert_mops.push_back(r.insert_mops);
    get_mops.push_back(r.get_mops);
    std::cout << "  trial " << t << ":  insert " << std::setw(6)
              << r.insert_mops << "  get " << std::setw(6) << r.get_mops
              << "\n";
  }

  TrialResult med{median(insert_mops), median(get_mops)};
  std::cout << "  median:   insert " << std::setw(6) << med.insert_mops
            << "  get " << std::setw(6) << med.get_mops << "\n\n";
  return med;
}

int main() {
  std::cout << std::fixed << std::setprecision(2);

  // Setup: all randomness generated before any clock starts
  auto start = std::chrono::steady_clock::now();
  std::vector<std::string> keys = generate_random_strings(NUM_KEYS);
  std::vector<uint32_t> lookup_order =
      generate_random_indices(NUM_GETS, NUM_KEYS);
  std::cout << "setup: " << NUM_KEYS << " keys, " << NUM_GETS << " gets, "
            << TRIALS << " trials (" << seconds_since(start) << " s)\n"
            << "all numbers in Mops/s\n\n";

  TrialResult kv = run_bench("KVStore", [&] {
    KVStore store(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (const auto &key : keys) {
      store.set(key, key);
    }
    double insert_s = seconds_since(t0);

    std::string out;
    size_t found = 0;
    t0 = std::chrono::steady_clock::now();
    for (uint32_t idx : lookup_order) {
      if (store.get(keys[idx], out)) {
        found++;
      }
    }
    double get_s = seconds_since(t0);

    if (found != static_cast<size_t>(NUM_GETS)) {
      std::cerr << "BUG: only found " << found << " of " << NUM_GETS << "\n";
      std::exit(1);
    }
    return TrialResult{NUM_KEYS / insert_s / 1e6, NUM_GETS / get_s / 1e6};
  });

  TrialResult um = run_bench("std::unordered_map", [&] {
    std::unordered_map<std::string, std::string> map;
    map.reserve(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (const auto &key : keys) {
      map[key] = key;
    }
    double insert_s = seconds_since(t0);

    size_t found = 0;
    t0 = std::chrono::steady_clock::now();
    for (uint32_t idx : lookup_order) {
      if (map.find(keys[idx]) != map.end()) {
        found++;
      }
    }
    double get_s = seconds_since(t0);

    if (found != static_cast<size_t>(NUM_GETS)) {
      std::cerr << "BUG: only found " << found << " of " << NUM_GETS << "\n";
      std::exit(1);
    }
    return TrialResult{NUM_KEYS / insert_s / 1e6, NUM_GETS / get_s / 1e6};
  });
  TrialResult kvo = run_bench("KVStoreOpen", [&] {
    KVStoreOpen store(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (const auto &key : keys) {
      store.set(key, key);
    }
    double insert_s = seconds_since(t0);

    std::string out;
    size_t found = 0;
    t0 = std::chrono::steady_clock::now();
    for (uint32_t idx : lookup_order) {
      if (store.get(keys[idx], out)) {
        found++;
      }
    }
    double get_s = seconds_since(t0);

    if (found != static_cast<size_t>(NUM_GETS)) {
      std::cerr << "BUG: only found " << found << " of " << NUM_GETS << "\n";
      std::exit(1);
    }
    return TrialResult{NUM_KEYS / insert_s / 1e6, NUM_GETS / get_s / 1e6};
  });

  std::cout << "summary (median of " << TRIALS << " trials, Mops/s)\n";
  std::cout << "  " << std::left << std::setw(22) << "store" << std::right
            << std::setw(8) << "insert" << std::setw(8) << "get" << "\n";
  std::cout << "  " << std::left << std::setw(22)
            << "KVStoreOpen (continous memory array)" << std::right
            << std::setw(8) << kvo.insert_mops << std::setw(8) << kvo.get_mops
            << "\n";
  std::cout << "  " << std::left << std::setw(22) << "KVStore (chaining)"
            << std::right << std::setw(8) << kv.insert_mops << std::setw(8)
            << kv.get_mops << "\n";
  std::cout << "  " << std::left << std::setw(22) << "std::unordered_map"
            << std::right << std::setw(8) << um.insert_mops << std::setw(8)
            << um.get_mops << "\n";

  return 0;
}
