#!/usr/bin/env python3
"""FrogJS against Node.js on the same machine: `make bench`. Each benchmark runs
in a fresh process, several times; the best time counts, which is the least noisy.
Ratio = FrogJS time / Node.js time (below 1 means FrogJS is faster). Exits with
code 1 if a ratio is over --max-ratio.
Usage: benchmark/run.py path/to/frogjs [--runs N] [--max-ratio R]"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def run(command, name):
    output = subprocess.run(command + [os.path.join(ROOT, 'bench.js'), name], capture_output=True, text=True, timeout=300)
    if output.returncode != 0:
        raise RuntimeError(f'{name}: {output.stderr[-500:]}')
    return float(output.stdout.strip().split('\n')[-1])


def startup(command, runs):
    best = float('inf')
    for _ in range(runs * 5):
        result = subprocess.run(command + ['-e', 'const t = process.hrtime()'], capture_output=True)
        if result.returncode != 0:
            raise RuntimeError('startup failed')
    import time
    for _ in range(runs * 5):
        start = time.perf_counter()
        subprocess.run(command + ['-e', '0'], capture_output=True)
        best = min(best, (time.perf_counter() - start) * 1000)
    return best


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('frogjs')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--max-ratio', type=float, default=0)
    parser.add_argument('--json')
    args = parser.parse_args()
    node = shutil.which('node')
    if node is None:
        print('node is not on PATH')
        return 1
    frog = [os.path.abspath(args.frogjs)]
    names = subprocess.check_output([node, os.path.join(ROOT, 'bench.js'), '--list'], text=True).split()

    print(f"{'benchmark':28} {'frogjs ms':>10} {'node ms':>10} {'ratio':>7}")
    results = {}
    worst = 0
    entries = [('startup', lambda c: startup(c, args.runs))] + [(n, lambda c, n=n: min(run(c, n) for _ in range(args.runs))) for n in names]
    for name, measure in entries:
        frog_ms = measure(frog)
        node_ms = measure([node])
        ratio = frog_ms / node_ms
        worst = max(worst, ratio)
        results[name] = {'frogjs': round(frog_ms, 2), 'node': round(node_ms, 2), 'ratio': round(ratio, 2)}
        print(f'{name:28} {frog_ms:10.1f} {node_ms:10.1f} {ratio:7.2f}', flush=True)
    print(f'\nworst ratio: {worst:.2f}')
    if args.json:
        json.dump(results, open(args.json, 'w'), indent=2)
    if args.max_ratio and worst > args.max_ratio:
        print(f'FAIL: over {args.max_ratio}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
