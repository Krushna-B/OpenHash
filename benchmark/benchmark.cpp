#include "kv_store.hpp"
#include "kv_store_arena.hpp"
#include "kv_store_open.hpp"
#include "locked_store.hpp"
#include "sharded_store.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

const int SEED = 42;
int NUM_KEYS = 1'000'000; // overridable via argv[2]
const int NUM_GETS = 10'000'000;
const int TRIALS = 5;
size_t INITIAL_BUCKETS = 2 << 18; // recomputed from NUM_KEYS in main
const size_t KEY_LENGTH = 16;
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

std::vector<std::string> generate_random_strings(int n, size_t length,
                                                 int seed) {
  std::vector<std::string> random_strings;
  random_strings.reserve(n);

  // C++ random engine
  std::mt19937 generator(seed); // fixed seed: same strings every run
  std::uniform_int_distribution<size_t> distribution(0, charset.size() - 1);

  for (size_t i{}; i < static_cast<size_t>(n); ++i) {
    std::string str(length, ' ');
    for (size_t j = 0; j < length; ++j) {
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

// Run trials; prints human output to stdout, appends one CSV row per trial
template <typename TrialFn>
TrialResult run_bench(const std::string &label, std::ofstream &csv,
                      size_t value_length, TrialFn run_trial) {
  std::vector<double> insert_mops;
  std::vector<double> get_mops;

  std::cout << label << "\n";
  for (int t = 1; t <= TRIALS; ++t) {
    TrialResult r = run_trial();
    insert_mops.push_back(r.insert_mops);
    get_mops.push_back(r.get_mops);
    std::cout << "  trial " << t << ":  insert " << std::setw(6)
              << r.insert_mops << "  get " << std::setw(6) << r.get_mops
              << "\n";
    csv << label << ',' << KEY_LENGTH << ',' << value_length << ',' << NUM_KEYS
        << ',' << NUM_GETS << ',' << INITIAL_BUCKETS << ',' << t << ','
        << r.insert_mops << ',' << r.get_mops << '\n';
  }

  TrialResult med{median(insert_mops), median(get_mops)};
  std::cout << "  median:   insert " << std::setw(6) << med.insert_mops
            << "  get " << std::setw(6) << med.get_mops << "\n\n";
  return med;
}

template <typename Store>
void run_threaded_bench(const std::string &label, std::ofstream &csv,
                        size_t value_length, int write_every,
                        const std::vector<std::string> &keys,
                        const std::vector<std::string> &values,
                        const std::vector<uint32_t> &lookup_order) {
  std::cout << label
            << (write_every ? " (mixed: 1 write per " +
                                  std::to_string(write_every) + " ops)"
                            : " (read-only)")
            << "\n";
  for (int T : {1, 2, 4, 8}) {
    std::vector<double> mops;
    for (int t = 1; t <= TRIALS; ++t) {
      Store store(INITIAL_BUCKETS);
      for (int k = 0; k < NUM_KEYS; ++k) {
        store.set(keys[k], values[k]);
      }

      std::atomic<size_t> total_found{0};
      size_t expected_reads = 0;
      for (size_t j = 0; j < lookup_order.size(); ++j) {
        if (!(write_every && j % write_every == 0)) {
          expected_reads++;
        }
      }

      auto t0 = std::chrono::steady_clock::now();
      std::vector<std::thread> threads;
      for (int w = 0; w < T; ++w) {
        threads.emplace_back([&, w] {
          std::string out;
          size_t found = 0;
          for (size_t j = w; j < lookup_order.size(); j += T) {
            uint32_t idx = lookup_order[j];
            if (write_every && j % write_every == 0) {
              store.set(keys[idx], values[idx]);
            } else if (store.get(keys[idx], out)) {
              found++;
            }
          }
          total_found += found;
        });
      }
      for (auto &th : threads) {
        th.join();
      }
      double secs = seconds_since(t0);

      if (total_found != expected_reads) {
        std::cerr << "BUG: found " << total_found << " of " << expected_reads
                  << "\n";
        std::exit(1);
      }
      mops.push_back(lookup_order.size() / secs / 1e6);
    }
    double med = median(mops);
    std::cout << "  threads " << T << ":  " << std::setw(6) << med
              << " Mops/s\n";
    csv << label << ',' << KEY_LENGTH << ',' << value_length << ',' << NUM_KEYS
        << ',' << NUM_GETS << ',' << T << ',' << write_every << ",median,"
        << med << '\n';
  }
  std::cout << "\n";
}

int main(int argc, char *argv[]) {
  size_t value_length = (argc > 1) ? std::stoul(argv[1]) : 64;
  if (argc > 2) {
    NUM_KEYS = std::stoi(argv[2]);
  }
  INITIAL_BUCKETS = static_cast<size_t>(NUM_KEYS) * 2;
  bool run_threads = (argc > 3 && std::string(argv[3]) == "threads");
  std::cout << std::fixed << std::setprecision(2);

  std::ofstream csv("results.csv", std::ios::app);
  csv << std::fixed << std::setprecision(2);
  if (csv.tellp() == 0) {
    csv << "store,key_len,value_len,num_keys,num_gets,slots,trial,insert_mops,"
           "get_mops\n";
  }

  // Setup: all randomness generated before any clock starts
  auto start = std::chrono::steady_clock::now();
  std::vector<std::string> keys =
      generate_random_strings(NUM_KEYS, KEY_LENGTH, SEED);
  std::vector<std::string> values =
      generate_random_strings(NUM_KEYS, value_length, SEED + 1);
  std::vector<uint32_t> lookup_order =
      generate_random_indices(NUM_GETS, NUM_KEYS);
  std::cout << "setup: " << NUM_KEYS << " keys (" << KEY_LENGTH << "B), values "
            << value_length << "B, " << NUM_GETS << " gets, " << TRIALS
            << " trials (" << seconds_since(start) << " s)\n"
            << "all numbers in Mops/s\n\n";

  TrialResult kv = run_bench("KVStore", csv, value_length, [&] {
    KVStore store(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < NUM_KEYS; ++k) {
      store.set(keys[k], values[k]);
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

  TrialResult um = run_bench("std::unordered_map", csv, value_length, [&] {
    std::unordered_map<std::string, std::string> map;
    map.reserve(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < NUM_KEYS; ++k) {
      map[keys[k]] = values[k];
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

  TrialResult kvo = run_bench("KVStoreOpen", csv, value_length, [&] {
    KVStoreOpen store(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < NUM_KEYS; ++k) {
      store.set(keys[k], values[k]);
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

  TrialResult kva = run_bench("KVStoreArena", csv, value_length, [&] {
    KVStoreArena store(INITIAL_BUCKETS);

    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < NUM_KEYS; ++k) {
      store.set(keys[k], values[k]);
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

  if (run_threads) {
    std::ofstream tcsv("results_threaded.csv", std::ios::app);
    tcsv << std::fixed << std::setprecision(2);
    if (tcsv.tellp() == 0) {
      tcsv << "store,key_len,value_len,num_keys,num_gets,threads,write_every,"
              "trial,mops\n";
    }

    run_threaded_bench<LockedStore<KVStoreArena>>(
        "LockedStore", tcsv, value_length, 0, keys, values, lookup_order);
    run_threaded_bench<LockedStore<KVStoreArena>>(
        "LockedStore", tcsv, value_length, 10, keys, values, lookup_order);
    run_threaded_bench<ShardedStore<KVStoreArena>>(
        "ShardedStore", tcsv, value_length, 0, keys, values, lookup_order);
    run_threaded_bench<ShardedStore<KVStoreArena>>(
        "ShardedStore", tcsv, value_length, 10, keys, values, lookup_order);
  }

  std::cout << "summary (median of " << TRIALS << " trials, Mops/s)\n";
  std::cout << "  " << std::left << std::setw(22) << "store" << std::right
            << std::setw(8) << "insert" << std::setw(8) << "get" << "\n";
  std::cout << "  " << std::left << std::setw(22) << "KVStore (chaining)"
            << std::right << std::setw(8) << kv.insert_mops << std::setw(8)
            << kv.get_mops << "\n";
  std::cout << "  " << std::left << std::setw(22) << "std::unordered_map"
            << std::right << std::setw(8) << um.insert_mops << std::setw(8)
            << um.get_mops << "\n";
  std::cout << "  " << std::left << std::setw(22) << "KVStoreOpen (probing)"
            << std::right << std::setw(8) << kvo.insert_mops << std::setw(8)
            << kvo.get_mops << "\n";
  std::cout << "  " << std::left << std::setw(22)
            << "KVStoreArena (arena allocator with open addressing)"
            << std::right << std::setw(8) << kva.insert_mops << std::setw(8)
            << kva.get_mops << "\n";

  return 0;
}
