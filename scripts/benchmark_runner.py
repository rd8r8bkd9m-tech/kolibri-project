#!/usr/bin/env python3
"""
Kolibri OS Performance Benchmark Runner

Runs comprehensive performance benchmarks and generates reports.
"""

import json
import subprocess
import time
import statistics
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any


class BenchmarkRunner:
    """Run and collect benchmark results."""
    
    def __init__(self, output_dir: str = "benchmark_results"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.results: Dict[str, Any] = {
            "timestamp": datetime.now().isoformat(),
            "benchmarks": {}
        }
    
    def run_c_benchmarks(self) -> Dict[str, float]:
        """Run C core benchmarks."""
        results = {}
        c_bench_dir = Path("benchmarks/c")
        
        if not c_bench_dir.exists():
            print("C benchmarks directory not found")
            return results
        
        # Build benchmarks
        try:
            subprocess.run(["make", "clean"], cwd=c_bench_dir, check=False)
            subprocess.run(["make", "all"], cwd=c_bench_dir, check=True, capture_output=True)
        except subprocess.CalledProcessError as e:
            print(f"Build failed: {e}")
            return results
        
        # Run individual benchmarks
        benchmark_executables = [
            "memcpy_test", "memset_test", "string_test",
            "math_test", "sort_test", "hash_test"
        ]
        
        for bench in benchmark_executables:
            exe_path = c_bench_dir / bench
            if exe_path.exists():
                try:
                    times = []
                    for _ in range(5):  # Run 5 iterations
                        start = time.perf_counter()
                        result = subprocess.run(
                            [str(exe_path)],
                            cwd=c_bench_dir,
                            capture_output=True,
                            timeout=60
                        )
                        elapsed = time.perf_counter() - start
                        times.append(elapsed)
                    
                    results[bench] = {
                        "mean": statistics.mean(times),
                        "median": statistics.median(times),
                        "std_dev": statistics.stdev(times) if len(times) > 1 else 0,
                        "min": min(times),
                        "max": max(times)
                    }
                    print(f"✓ {bench}: {results[bench]['mean']*1000:.2f}ms")
                except Exception as e:
                    print(f"✗ {bench} failed: {e}")
        
        return results
    
    def run_python_benchmarks(self) -> Dict[str, float]:
        """Run Python benchmarks using pytest-benchmark."""
        results = {}
        
        try:
            result = subprocess.run(
                ["pytest", "benchmarks/python", "--benchmark-json=python_bench.json", "-v"],
                capture_output=True,
                text=True
            )
            
            bench_file = Path("python_bench.json")
            if bench_file.exists():
                with open(bench_file) as f:
                    data = json.load(f)
                    for bench in data.get("benchmarks", []):
                        results[bench["name"]] = {
                            "mean": bench["stats"]["mean"],
                            "median": bench["stats"]["median"],
                            "std_dev": bench["stats"]["stddev"],
                            "min": bench["stats"]["min"],
                            "max": bench["stats"]["max"],
                            "iterations": bench["stats"]["iterations"]
                        }
                bench_file.unlink()
        except Exception as e:
            print(f"Python benchmarks failed: {e}")
        
        return results
    
    def run_gpu_benchmarks(self) -> Dict[str, float]:
        """Run GPU/OpenCL benchmarks if available."""
        results = {}
        gpu_bench_dir = Path("benchmarks/gpu_opencl")
        
        if not gpu_bench_dir.exists():
            print("GPU benchmarks directory not found")
            return results
        
        # Try to run OpenCL benchmarks
        try:
            result = subprocess.run(
                ["make", "run"],
                cwd=gpu_bench_dir,
                capture_output=True,
                text=True,
                timeout=120
            )
            
            # Parse output for timing information
            for line in result.stdout.split('\n'):
                if 'time:' in line.lower() or 'ms' in line.lower():
                    # Extract benchmark name and time
                    parts = line.split(':')
                    if len(parts) >= 2:
                        name = parts[0].strip()
                        try:
                            time_val = float(parts[1].replace('ms', '').strip())
                            results[name] = {"mean": time_val / 1000.0}  # Convert to seconds
                        except ValueError:
                            pass
        except Exception as e:
            print(f"GPU benchmarks failed (expected if no GPU): {e}")
        
        return results
    
    def generate_report(self) -> Path:
        """Generate comprehensive benchmark report."""
        # Run all benchmarks
        print("\n=== Running C Benchmarks ===")
        self.results["benchmarks"]["c"] = self.run_c_benchmarks()
        
        print("\n=== Running Python Benchmarks ===")
        self.results["benchmarks"]["python"] = self.run_python_benchmarks()
        
        print("\n=== Running GPU Benchmarks ===")
        self.results["benchmarks"]["gpu"] = self.run_gpu_benchmarks()
        
        # Save JSON report
        json_path = self.output_dir / f"benchmark_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        with open(json_path, 'w') as f:
            json.dump(self.results, f, indent=2)
        
        # Generate summary
        summary = self._generate_summary()
        summary_path = self.output_dir / f"summary_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"
        with open(summary_path, 'w') as f:
            f.write(summary)
        
        print(f"\n✓ Reports saved to {self.output_dir}/")
        return json_path
    
    def _generate_summary(self) -> str:
        """Generate markdown summary of benchmark results."""
        lines = [
            "# Kolibri OS Performance Benchmark Summary",
            f"\n**Date:** {self.results['timestamp']}\n",
            "## C Core Benchmarks\n",
            "| Test | Mean (ms) | Median (ms) | Std Dev |\n",
            "|------|-----------|-------------|---------|"
        ]
        
        for name, data in self.results["benchmarks"].get("c", {}).items():
            lines.append(
                f"| {name} | {data['mean']*1000:.3f} | {data['median']*1000:.3f} | {data['std_dev']*1000:.3f} |"
            )
        
        lines.extend([
            "\n## Python Benchmarks\n",
            "| Test | Mean (s) | Iterations |\n",
            "|------|----------|------------|"
        ])
        
        for name, data in self.results["benchmarks"].get("python", {}).items():
            lines.append(
                f"| {name} | {data['mean']:.6f} | {data.get('iterations', 'N/A')} |"
            )
        
        return '\n'.join(lines)


def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Kolibri OS Benchmark Runner")
    parser.add_argument("--output-dir", default="benchmark_results",
                       help="Output directory for results")
    parser.add_argument("--category", choices=["c", "python", "gpu", "all"],
                       default="all", help="Benchmark category to run")
    args = parser.parse_args()
    
    runner = BenchmarkRunner(output_dir=args.output_dir)
    
    if args.category == "all":
        runner.generate_report()
    elif args.category == "c":
        results = runner.run_c_benchmarks()
        print(json.dumps(results, indent=2))
    elif args.category == "python":
        results = runner.run_python_benchmarks()
        print(json.dumps(results, indent=2))
    elif args.category == "gpu":
        results = runner.run_gpu_benchmarks()
        print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
