// Memory-hierarchy probe: get throughput AND dependent-chain latency vs
// working-set size. Sweeps table sizes across L1 (128KiB), L2 (16MB), DRAM.
#include "kv_store_open.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

const int SEED = 42;
const int TRIALS = 3;
const long NUM_OPS = 10'000'000;
const size_t KEY_LENGTH = 16;
const std::string charset =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// bytes per key: 2 slots/key at load 0.5, sizeof(Slot)=56, +24 for the
// keys[] lookup vector entry (SSO keeps all string bytes inline)
const size_t BYTES_PER_KEY = 2 * 56 + 24;

double seconds_since(std::chrono::steady_clock::time_point start) {
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(end - start).count();
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  size_t mid = v.size() / 2;
  return v.size() % 2 ? v[mid] : (v[mid - 1] + v[mid]) / 2;
}

std::vector<std::string> generate_random_strings(size_t n) {
  std::vector<std::string> strings;
  strings.reserve(n);
  std::mt19937 generator(SEED);
  std::uniform_int_distribution<size_t> distribution(0, charset.size() - 1);
  for (size_t i = 0; i < n; ++i) {
    std::string str(KEY_LENGTH, ' ');
    for (size_t j = 0; j < KEY_LENGTH; ++j) {
      str[j] = charset[distribution(generator)];
    }
    strings.push_back(std::move(str));
  }
  return strings;
}

// one full cycle over 0..n-1 (Sattolo's algorithm)
std::vector<uint32_t> make_cycle(size_t n) {
  std::vector<uint32_t> perm(n);
  std::iota(perm.begin(), perm.end(), 0);
  std::mt19937 generator(SEED + 7);
  for (size_t i = n - 1; i > 0; --i) {
    std::uniform_int_distribution<size_t> d(0, i - 1);
    std::swap(perm[i], perm[d(generator)]);
  }
  std::vector<uint32_t> next(n);
  for (size_t i = 0; i + 1 < n; ++i) {
    next[perm[i]] = perm[i + 1];
  }
  next[perm[n - 1]] = perm[0];
  return next;
}

// cheap inline PRNG
inline uint32_t xorshift(uint32_t &x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

int main() {
  std::cout << std::fixed << std::setprecision(2);

  std::ofstream csv("results_hierarchy.csv", std::ios::app);
  csv << std::fixed << std::setprecision(2);
  if (csv.tellp() == 0) {
    csv << "bytes,num_keys,trial,tput_mops,tput_ns_per_op,chase_ns_per_op\n";
  }

  std::vector<size_t> target_bytes = {
      32 << 10, 64 << 10, 128 << 10, 256 << 10, 512 << 10, 2 << 20,
      8 << 20,  16 << 20, 32 << 20,  128 << 20, 512 << 20};

  std::cout << std::setw(10) << "bytes" << std::setw(10) << "keys"
            << std::setw(12) << "tput Mops" << std::setw(10) << "tput ns"
            << std::setw(10) << "chase ns" << "\n";

  for (size_t bytes : target_bytes) {
    size_t n = std::max<size_t>(64, bytes / BYTES_PER_KEY);
    std::vector<std::string> keys = generate_random_strings(n);
    std::vector<uint32_t> next = make_cycle(n);

    std::vector<double> tput_mops, tput_ns, chase_ns;
    for (int t = 1; t <= TRIALS; ++t) {
      KVStoreOpen store(n * 2);
      for (size_t i = 0; i < n; ++i) {
        store.set(keys[i], std::to_string(next[i]));
      }

      // throughput: independent random gets, misses overlap
      std::string out;
      size_t found = 0;
      uint32_t rng = 0x9e3779b9 + t;
      auto t0 = std::chrono::steady_clock::now();
      for (long op = 0; op < NUM_OPS; ++op) {
        if (store.get(keys[xorshift(rng) % n], out)) {
          found++;
        }
      }
      double tput_s = seconds_since(t0);

      // latency: dependent chain, each get's key comes from the last value
      uint32_t idx = 0;
      size_t chased = 0;
      t0 = std::chrono::steady_clock::now();
      for (long op = 0; op < NUM_OPS; ++op) {
        if (store.get(keys[idx], out)) {
          chased++;
        }
        idx = static_cast<uint32_t>(std::stoul(out));
      }
      double chase_s = seconds_since(t0);

      if (found != static_cast<size_t>(NUM_OPS) ||
          chased != static_cast<size_t>(NUM_OPS)) {
        std::cerr << "BUG: found " << found << " chased " << chased << "\n";
        return 1;
      }

      tput_mops.push_back(NUM_OPS / tput_s / 1e6);
      tput_ns.push_back(tput_s * 1e9 / NUM_OPS);
      chase_ns.push_back(chase_s * 1e9 / NUM_OPS);
      csv << bytes << ',' << n << ',' << t << ',' << tput_mops.back() << ','
          << tput_ns.back() << ',' << chase_ns.back() << '\n';
    }

    std::cout << std::setw(10) << bytes << std::setw(10) << n << std::setw(12)
              << median(tput_mops) << std::setw(10) << median(tput_ns)
              << std::setw(10) << median(chase_ns) << "\n";
  }

  return 0;
}
