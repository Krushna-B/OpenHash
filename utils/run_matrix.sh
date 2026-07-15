#!/bin/bash
# Full benchmark matrix: every store x key count x value size.
# Single-threaded stores run at every point; thread scaling runs at 1M keys
# with small and large values. ~30-60 min total. Run from anywhere.
set -e
cd "$(dirname "$0")/.."

cmake --build build

# fresh data: this script defines the canonical methodology
rm -f results.csv results_threaded.csv

for keys in 10000 100000 1000000; do
  for vlen in 16 64 256; do
    echo "=== keys=$keys value_len=$vlen ==="
    ./build/Benchmark "$vlen" "$keys"
  done
done

echo "=== thread scaling, 1M keys ==="
./build/Benchmark 16 1000000 threads
./build/Benchmark 256 1000000 threads

echo "done: results.csv + results_threaded.csv"
