"""Condition-latched timestamp ring regression using the actual publication, lookup and report callback.

Usage: python tests/nvn_submit_ticks_test.py --cc /path/to/clang [--revision BASELINE]
The timeout detects a deadlock; assertions use lock/condition handshakes, never elapsed time.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def selected_source(path, revision):
    if revision:
        return subprocess.check_output(['git', 'show', f'{revision}:{path}'], cwd=ROOT, text=True)
    return (ROOT/path).read_text()


def extract_source(core, xfer):
    ring = core[core.index('#define SUBMIT_TICKS '):core.index('\nNVN_API(nvnQueueSubmitCommands)')]
    if 'static void submit_ticks_publish(' not in ring:
        # Before the fix the exact two publication statements lived inside QueueSubmitCommands.
        start = core.index('    __atomic_store_n(&submit_fence[')
        end = core.index('\n', core.index('__atomic_store_n(&submit_tick[', start))
        ring += '\nstatic void submit_ticks_publish(uint64_t f, uint64_t submitted) {\n' + core[start:end] + '\n}\n'
    ring, reads = re.subn(r'(    uint64_t t = [^;]+;)', r'\1\n    test_after_tick_read();', ring)
    ring, writes = re.subn(
        r'(    (?:__atomic_store_n\(&submit_fence\[[^\n]+|submit_fence\[[^\n]+);)',
        r'\1\n    test_after_tag_store();', ring)
    if reads != 1 or writes != 1:
        raise ValueError('timestamp source changed: expected one lookup read and one publication tag store')
    reports = xfer[xfer.index('static void write_counter_at('):xfer.index('\n// Reports queued by the replay')]
    return ring + '\n' + reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang'))
    parser.add_argument('--revision', help='read production source from this Git revision instead of the working tree')
    args = parser.parse_args()
    if not args.cc:
        parser.error('pass --cc with the project Clang executable')
    revision = subprocess.check_output(
        ['git', 'rev-parse', '--verify', '--end-of-options', args.revision + '^{commit}'],
        cwd=ROOT, text=True).strip() if args.revision else None
    label = revision[:12] if revision else 'working'
    out = ROOT/'build'/'tests'/'nvn-submit-ticks'/label
    out.mkdir(parents=True, exist_ok=True)
    core = selected_source('runtime/nvn_api_core.c', revision)
    xfer = selected_source('runtime/vk_exec_xfer.c', revision)
    extracted = extract_source(core, xfer)
    (out/'timestamp_source.inc').write_text(extracted)
    results = []
    for optimization in ('-O0', '-O2'):
        exe = out/('nvn_submit_ticks' + optimization[1:] + ('.exe' if os.name == 'nt' else ''))
        command = [args.cc, '-std=gnu11', optimization, '-Wall', '-Wextra', '-Werror',
                   '-I', str(ROOT/'runtime'), '-I', str(out), str(ROOT/'tests/nvn_submit_ticks.c'),
                   *([] if os.name == 'nt' else ['-pthread']), '-o', str(exe)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        log = result.stdout + result.stderr
        (out/(optimization[1:] + '.log')).write_text(log)
        print(optimization + ': ' + log, end='')
        results.append({'optimization': optimization, 'exit': result.returncode, 'output': log})
    (out/'results.json').write_text(json.dumps({
        'revision': revision or 'working tree',
        'core_sha256': hashlib.sha256(core.encode()).hexdigest(),
        'xfer_sha256': hashlib.sha256(xfer.encode()).hexdigest(),
        'instrumented_extraction_sha256': hashlib.sha256(extracted.encode()).hexdigest(),
        'runs': results,
    }, indent=2) + '\n')
    return int(any(result['exit'] for result in results))


if __name__ == '__main__':
    raise SystemExit(main())
