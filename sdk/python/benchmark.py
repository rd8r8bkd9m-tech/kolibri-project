#!/usr/bin/env python3
"""
Kolibri SDK Benchmark: Pure Python vs C Extension

Compares encoding/decoding speed between:
1. Pure Python implementation (baseline)
2. C Extension (Kolibri optimized)

Run with: python benchmark.py
"""

import time
import statistics
import sys
import os

# Ensure we import from the local package
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from kolibri import encode, decode, is_c_extension_available
from kolibri.pure_python import encode_pure, decode_pure


def format_throughput(bytes_per_second: float) -> str:
    """Format throughput with appropriate units."""
    mb_s = bytes_per_second / (1024 * 1024)
    gb_s = bytes_per_second / (1024 * 1024 * 1024)

    if gb_s >= 1.0:
        return f"{gb_s:.2f} GB/s"
    return f"{mb_s:.2f} MB/s"


def benchmark(func, data, iterations=10, warmup=3):
    """
    Run benchmark with warmup and statistics.

    Args:
        func: Function to benchmark
        data: Input data
        iterations: Number of benchmark iterations
        warmup: Number of warmup iterations

    Returns:
        dict with timing statistics and throughput
    """
    # Warmup
    for _ in range(warmup):
        func(data)

    # Measurement iterations
    times = []
    for _ in range(iterations):
        start = time.perf_counter()
        result = func(data)
        end = time.perf_counter()
        times.append(end - start)

    avg_time = statistics.mean(times)
    data_size = len(data)

    return {
        'min': min(times),
        'max': max(times),
        'avg': avg_time,
        'stddev': statistics.stdev(times) if len(times) > 1 else 0,
        'throughput_b_s': data_size / avg_time if avg_time > 0 else 0,
        'throughput_mb_s': data_size / avg_time / 1024 / 1024 if avg_time > 0 else 0,
        'throughput_gb_s': data_size / avg_time / 1024 / 1024 / 1024 if avg_time > 0 else 0,
    }


def print_result(name: str, result: dict):
    """Print benchmark result in a formatted way."""
    throughput = format_throughput(result['throughput_b_s'])
    print(f"  {name:20s}: {throughput:>12s} "
          f"(avg: {result['avg']*1000:.2f}ms, stddev: {result['stddev']*1000:.2f}ms)")


def run_encode_benchmark(data: bytes, size_name: str):
    """Run encoding benchmarks for both implementations."""
    print(f"\n{'='*60}")
    print(f"ENCODE Benchmark: {size_name} ({len(data):,} bytes)")
    print('='*60)

    results = {}

    # Pure Python
    try:
        iters = 3 if len(data) > 1024*1024 else 10
        results['pure'] = benchmark(encode_pure, data, iterations=iters, warmup=1)
        print_result("Pure Python", results['pure'])
    except Exception as e:
        print(f"  Pure Python: SKIP ({e})")

    # C Extension
    if is_c_extension_available():
        try:
            results['c'] = benchmark(encode, data, iterations=10, warmup=3)
            print_result("C Extension", results['c'])
        except Exception as e:
            print(f"  C Extension: ERROR ({e})")
    else:
        print("  C Extension: NOT AVAILABLE (run 'pip install -e .' to build)")

    # Speedup
    if 'pure' in results and 'c' in results:
        speedup = results['c']['throughput_b_s'] / results['pure']['throughput_b_s']
        print(f"\n  Speedup: {speedup:.1f}x faster!")

    return results


def run_decode_benchmark(encoded: str, size_name: str):
    """Run decoding benchmarks for both implementations."""
    print(f"\n{'='*60}")
    print(f"DECODE Benchmark: {size_name} ({len(encoded):,} chars)")
    print('='*60)

    results = {}

    # Pure Python
    try:
        iters = 3 if len(encoded) > 3*1024*1024 else 10
        results['pure'] = benchmark(decode_pure, encoded, iterations=iters, warmup=1)
        print_result("Pure Python", results['pure'])
    except Exception as e:
        print(f"  Pure Python: SKIP ({e})")

    # C Extension
    if is_c_extension_available():
        try:
            results['c'] = benchmark(decode, encoded, iterations=10, warmup=3)
            print_result("C Extension", results['c'])
        except Exception as e:
            print(f"  C Extension: ERROR ({e})")
    else:
        print("  C Extension: NOT AVAILABLE")

    # Speedup
    if 'pure' in results and 'c' in results:
        speedup = results['c']['throughput_b_s'] / results['pure']['throughput_b_s']
        print(f"\n  Speedup: {speedup:.1f}x faster!")

    return results


def main():
    print("=" * 60)
    print("KOLIBRI SDK BENCHMARK: Pure Python vs C Extension")
    print("=" * 60)

    # Show C extension status
    if is_c_extension_available():
        print("\n✓ C Extension: AVAILABLE")
    else:
        print("\n✗ C Extension: NOT AVAILABLE")
        print("  To build: cd sdk/python && pip install -e .")

    # Test data sizes
    sizes = [
        ("1 KB", 1024),
        ("10 KB", 10 * 1024),
        ("100 KB", 100 * 1024),
        ("1 MB", 1024 * 1024),
    ]

    # Add larger sizes only if C extension is available
    if is_c_extension_available():
        sizes.extend([
            ("10 MB", 10 * 1024 * 1024),
            ("100 MB", 100 * 1024 * 1024),
        ])

    all_encode_results = {}
    all_decode_results = {}

    for name, size in sizes:
        # Generate test data - repeating byte pattern
        data = bytes(range(256)) * (size // 256 + 1)
        data = data[:size]

        # Run encode benchmark
        encode_results = run_encode_benchmark(data, name)
        all_encode_results[name] = encode_results

        # Generate encoded data for decode benchmark
        encoded = encode_pure(data)

        # Run decode benchmark
        decode_results = run_decode_benchmark(encoded, name)
        all_decode_results[name] = decode_results

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    if is_c_extension_available():
        print("\n┌─────────────┬────────────────┬────────────────┬──────────┐")
        print("│ Size        │ Pure Python    │ C Extension    │ Speedup  │")
        print("├─────────────┼────────────────┼────────────────┼──────────┤")

        for name, results in all_encode_results.items():
            if 'pure' in results and 'c' in results:
                pure_tp = format_throughput(results['pure']['throughput_b_s'])
                c_tp = format_throughput(results['c']['throughput_b_s'])
                speedup = results['c']['throughput_b_s'] / results['pure']['throughput_b_s']
                print(f"│ {name:11s} │ {pure_tp:>14s} │ {c_tp:>14s} │ {speedup:>6.1f}x │")

        print("└─────────────┴────────────────┴────────────────┴──────────┘")
    else:
        print("\nNote: Build the C extension to see performance comparison.")
        print("      cd sdk/python && pip install -e .")

    print()


if __name__ == "__main__":
    main()
