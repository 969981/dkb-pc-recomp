"""The staged uploads' blocked-byte lookup against the union it replaced, on random inputs. No GPU or game runs.

Usage: python tests/staged_blocked_test.py --cc clang [--out DIR] [--cases N] [--seed S] [--source vk_memory.c]

With TOTK_MIRROR_STAGED a staged upload leaves out the bytes of the VRAM mirror the GPU owns. Before commit ce4178e
staged_upload built one sorted union of the pools' own lists and the lists of every write-back job in flight
(st_blk_update) and cut each upload range against it; since then st_blocked and st_job_blocked find the blocked
pieces by binary search in each list and st_cut cuts the range against them. tests/staged_blocked.c runs the
production lookup, cut, sort (st_sort_job) and own-list code from runtime/vk_memory.c, pasted in below, against the old
union kept in the fixture, and requires the same blocked bytes, copied pieces and byte counts for every upload range
of every case. The check TOTK_STAGED_BLK_CHECK makes in a game run (st_chk_union, st_chk_range) runs on the same ranges
and must flag none. A mismatch prints the seed and a shrunk counterexample.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from nvn2_compute_c0_test import function
from descriptor_pool_registry_test import typedef

ROOT = Path(__file__).resolve().parents[1]


def definition(source, start):
    """The function whose signature begins with start, skipping forward declarations."""
    at = 0
    while True:
        at = source.index(start, at)
        brace, semi = source.find('{', at), source.find(';', at)
        if brace >= 0 and (semi < 0 or brace < semi):
            return function(source[at:], start)
        at = semi


def line(source, start):
    """The whole line of source that starts with start."""
    begin = source.index('\n' + start) + 1
    return source[begin:source.index('\n', begin)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang'))
    parser.add_argument('--out', type=Path, default=ROOT / 'build' / 'tests' / 'staged-blocked')
    parser.add_argument('--cases', type=int, default=50000)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--source', type=Path, default=ROOT / 'runtime/vk_memory.c',
                        help='the vk_memory.c to take the code from (a changed copy, to see the test fail)')
    args = parser.parse_args()
    if not args.cc:
        parser.error('pass --cc with a C compiler')
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    src = args.source.read_text(encoding='utf-8')
    types = '\n'.join([typedef(src, 'VPage'), typedef(src, 'PoolRec'), typedef(src, 'MRange'),
                       typedef(src, 'StRange'), typedef(src, 'StJob')])
    code = '\n'.join([
        'static void grow(void **arr, int *cap, size_t elem);',
        definition(src, 'static void grow('),
        definition(src, 'static void range_add('),
        definition(src, 'static void own_listed('),
        definition(src, 'static void own_merge('),
        definition(src, 'static void own_cut('),
        line(src, '#define STRANGE_LESS('), line(src, 'MSORT_DEFINE(sort_st_ranges,'),
        definition(src, 'static void st_sort_job('),
        line(src, '#define KEPT_LESS('), line(src, 'MSORT_DEFINE(sort_kept,'),
        definition(src, 'static void st_job_blocked('),
        definition(src, 'static void st_blocked('),
        definition(src, 'static void st_cut('),
        line(src, '#define MRANGE_LESS('), line(src, 'MSORT_DEFINE(sort_ranges,'),
        # TOTK_STAGED_BLK_CHECK: the union and the comparison as the game runs them
        src[src.index('// ---- TOTK_STAGED_BLK_CHECK=1'):src.index('static void st_chk_log(')],
        definition(src, 'static void st_chk_log('),
    ])
    fixture = args.out / 'staged_blocked.c'
    fixture.write_text((ROOT / 'tests/staged_blocked.c').read_text(encoding='utf-8')
                       .replace('/* PRODUCTION_TYPES */', types).replace('/* PRODUCTION_FUNCTIONS */', code),
                       encoding='utf-8')
    for opt in ('-O0', '-O2'):
        exe = args.out / ('staged_blocked' + opt + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([args.cc, '-std=gnu11', opt, '-Wall', '-Wextra', '-Wno-missing-field-initializers', '-Werror',
                        '-I', str(ROOT / 'runtime'), str(fixture), '-o', str(exe)], check=True, timeout=120)
        run = subprocess.run([str(exe), str(args.cases), str(args.seed)], capture_output=True, text=True, timeout=600)
        print(opt, run.stdout.strip())
        if run.returncode:
            print(run.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
