#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

class Arena {
  static constexpr std::size_t CHUNK_SIZE = 64 * 1024; // 65kb
  std::vector<std::unique_ptr<char[]>> chunks; // Pointer to the allocated
                                               // memory
  char *current = nullptr;
  size_t remaining{};

public:
  char *alloc(size_t n) {
    n = (n + 7) &
        ~size_t(
            7); // Bit mask to make n divisbile by 8 so faster for CPU to acesse
    if (n > remaining) {
      std::unique_ptr<char[]> chunk =
          std::make_unique<char[]>(std::max(n, CHUNK_SIZE));
      remaining = std::max(n, CHUNK_SIZE);
      current = chunk.get();
      chunks.push_back(std::move(chunk));
    }
    char *result = current;
    current += n;
    remaining -= n;
    return result;
  }
};