#!/usr/bin/env python3
"""Line coverage of the runtime: `make coverage`.

Runs the whole test suite on an instrumented build and combines
 - C++: llvm-cov over the code outside the embedded JavaScript
 - embedded JavaScript: V8's precise coverage (written by src/utils/coverage.cpp)

JavaScript lines count as covered if V8 executed anything on them, the method of
c8 and v8-to-istanbul. Worker threads' JavaScript isn't recorded (only the main
thread of each process is), so code that only runs in workers counts as uncovered.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JS_BLOCK = re.compile(r'static const char\* (\w+) = R"JS\(|const char\* (\w+) = R"JS\(')
TESTS = ['hello', 'test-buffer', 'test-path', 'test-os', 'test-modules', 'test-events', 'test-streams', 'test-util',
         'test-http', 'test-crypto', 'test-child-process', 'test-worker-threads', 'test-fs', 'test-net', 'test-process',
         'test-timers', 'test-module-loading', 'test-errors', 'test-http-limits', 'test-edges', 'test-memory']


def tool(name):
    return subprocess.check_output(['xcrun', '-f', name], text=True).strip() if shutil.which('xcrun') else name


def run_tests(binary, profile_dir, js_dir):
    env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(profile_dir, '%p-%m.profraw'), FROGJS_COVERAGE_DIR=js_dir)
    timeout = ['perl', '-e', 'alarm shift; exec @ARGV', '600']
    for test in TESTS:
        print('  running', test, flush=True)
        result = subprocess.run(timeout + [binary, f'examples/{test}.js'], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors='replace')
        last_line = result.stdout.strip().split('\n')[-1]
        if result.returncode != 0 or (test != 'hello' and not last_line.endswith(' 0 failed')):
            print(result.stdout[-3000:])
            print(f'FAIL examples/{test}.js ({result.returncode}): {last_line}')
            return False
    for command in (['./examples/test-cli.sh', binary], ['python3', 'examples/test-repl.py', binary]):
        print('  running', command[-2], flush=True)
        result = subprocess.run(timeout + command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors='replace')
        if result.returncode != 0:
            print(result.stdout[-3000:])
            return False
    return True


def embedded_blocks():
    """Every R"JS(...)JS" block: its text and where it is, by the name it runs under"""
    blocks = {}
    names = {}
    for cpp in glob.glob(os.path.join(ROOT, 'src/**/*.cpp'), recursive=True):
        text = open(cpp, encoding='utf8').read()
        for match in re.finditer(r'RunJsModule\([^,]+,\s*[^,]+,\s*"([\w_]+)",\s*(\w+)', text):
            names[match.group(2)] = match.group(1)
        for match in JS_BLOCK.finditer(text):
            variable = match.group(1) or match.group(2)
            start = match.end()
            end = text.index(')JS"', start)
            blocks[variable] = (os.path.relpath(cpp, ROOT), text[start:end], text.count('\n', 0, start))
    return {names[v]: blocks[v] for v in blocks if v in names}, blocks


def is_code_line(line):
    stripped = line.strip()
    return bool(stripped) and not stripped.startswith('//') and stripped not in ('}', '{', '});', '})', '};', ')', ');', '],', '];')


def js_coverage(js_dir, blocks):
    """{ name: [covered char flags] } merged across every run: a char is covered if
    any process ran it"""
    covered = {name: bytearray(len(blocks[name][1])) for name in blocks}
    runs = 0
    for path in glob.glob(os.path.join(js_dir, 'js-*.json')):
        try:
            data = json.load(open(path))
        except ValueError:
            continue
        runs += 1
        for script in data.get('result', {}).get('result', []):
            url = script.get('url', '')
            if not url.startswith('frogjs:'):
                continue
            name = url[len('frogjs:'):]
            if name not in covered:
                continue
            flags = bytearray(len(blocks[name][1]))
            # Ranges nest: outer first, inner ranges overwrite
            for function in sorted(script['functions'], key=lambda f: (f['ranges'][0]['startOffset'], -f['ranges'][0]['endOffset'])):
                for rng in function['ranges']:
                    value = 1 if rng['count'] > 0 else 0
                    start = min(rng['startOffset'], len(flags))
                    end = min(rng['endOffset'], len(flags))
                    flags[start:end] = bytes([value]) * (end - start)
            merged = covered[name]
            for i, flag in enumerate(flags):
                if flag:
                    merged[i] = 1
    return covered, runs


def js_report(covered, blocks):
    rows = []
    uncovered = {}
    for name, (cpp, text, _) in sorted(blocks.items()):
        flags = covered[name]
        position = 0
        total = hit = 0
        missing = []
        for number, line in enumerate(text.split('\n')):
            length = len(line)
            if is_code_line(line):
                total += 1
                if any(flags[position + i] for i in range(length) if not line[i].isspace()):
                    hit += 1
                else:
                    missing.append(number + 1)
            position += length + 1
        rows.append((name, cpp, hit, total))
        uncovered[name] = missing
    return rows, uncovered


def cpp_report(binary, profile_dir, all_blocks):
    profdata = os.path.join(profile_dir, 'merged.profdata')
    raw = glob.glob(os.path.join(profile_dir, '*.profraw'))
    subprocess.check_call([tool('llvm-profdata'), 'merge', '-sparse', *raw, '-o', profdata])
    report = subprocess.check_output([tool('llvm-cov'), 'export', binary, f'-instr-profile={profdata}', '-summary-only=false',
                                      '-skip-expansions', '-skip-branches', f'-ignore-filename-regex=(/opt/homebrew|/usr/|/Library/)'], text=True)
    data = json.loads(report)
    rows = []
    js_lines = {}
    for variable, (cpp, text, first_line) in all_blocks.items():
        js_lines.setdefault(cpp, set()).update(range(first_line + 1, first_line + 2 + text.count('\n')))
    for entry in data['data'][0]['files']:
        relative = os.path.relpath(entry['filename'], ROOT)
        if relative.startswith('..') or not relative.endswith('.cpp') or relative.endswith('coverage.cpp'):
            continue
        # Line execution from the segments: a line is covered if a region on it ran
        lines = {}
        segments = entry['segments']
        current = 0
        for i, segment in enumerate(segments):
            line, _, count, has_count, is_entry = segment[:5]
            next_line = segments[i + 1][0] if i + 1 < len(segments) else line
            if has_count:
                current = count
                for number in range(line, next_line + 1):
                    if number == line or number < next_line:
                        lines[number] = max(lines.get(number, 0), count)
        skip = js_lines.get(relative, set())
        source = open(os.path.join(ROOT, relative), encoding='utf8').read().split('\n')
        total = hit = 0
        for number, count in lines.items():
            if number in skip or number > len(source):
                continue
            if not source[number - 1].strip() or source[number - 1].strip() in ('}', '{'):
                continue
            total += 1
            if count > 0:
                hit += 1
        rows.append((relative, hit, total))
    # llvm-cov's own total, as a check on the numbers above. It also counts comment
    # lines inside functions, so the two differ a little.
    summary = data['data'][0]['totals']['lines']
    print(f"\nllvm-cov's own line total (whole program): {summary['percent']:.1f}% ({summary['covered']}/{summary['count']})")
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--min', type=float, default=0)
    parser.add_argument('--show-missing', action='store_true')
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)

    work = tempfile.mkdtemp(prefix='frogjs-coverage-')
    profile_dir = os.path.join(work, 'profile')
    js_dir = os.path.join(work, 'js')
    os.makedirs(profile_dir)
    os.makedirs(js_dir)
    try:
        if not run_tests(binary, profile_dir, js_dir):
            return 1
        blocks, all_blocks = embedded_blocks()
        covered, runs = js_coverage(js_dir, blocks)
        js_rows, missing = js_report(covered, blocks)
        cpp_rows = cpp_report(binary, profile_dir, all_blocks)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print(f'\nEmbedded JavaScript ({runs} processes)')
    for name, cpp, hit, total in js_rows:
        print(f'  {name:18} {hit:5}/{total:<5} {100 * hit / max(total, 1):6.1f}%')
    js_hit = sum(r[2] for r in js_rows)
    js_total = sum(r[3] for r in js_rows)
    print('\nC++ (outside the embedded JavaScript)')
    for name, hit, total in sorted(cpp_rows):
        print(f'  {name:34} {hit:5}/{total:<5} {100 * hit / max(total, 1):6.1f}%')
    cpp_hit = sum(r[1] for r in cpp_rows)
    cpp_total = sum(r[2] for r in cpp_rows)

    if args.show_missing:
        print('\nUncovered JavaScript lines')
        for name, lines in missing.items():
            source = blocks[name][1].split('\n')
            for number in lines:
                print(f'  {name}:{number}: {source[number - 1].strip()[:100]}')

    total_pct = 100 * (js_hit + cpp_hit) / max(js_total + cpp_total, 1)
    print(f'\nJavaScript {100 * js_hit / max(js_total, 1):.1f}%   C++ {100 * cpp_hit / max(cpp_total, 1):.1f}%   '
          f'Total {total_pct:.1f}% ({js_hit + cpp_hit}/{js_total + cpp_total} lines)')
    if total_pct < args.min:
        print(f'FAIL: below the required {args.min}%')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
