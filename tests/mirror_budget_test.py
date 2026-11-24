"""The pool mirror budget rules (runtime/mirror_budget.h) compiled alone, without game files or a GPU.

Usage: python tests/mirror_budget_test.py --cc <clang>
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    args = ap.parse_args()
    out = ROOT / 'build/tests/mirror-budget'
    out.mkdir(parents=True, exist_ok=True)
    for opt in ('-O0', '-O2'):
        exe = out / f'mirror-budget-{opt[1:]}.exe'
        subprocess.run([args.cc, '-std=gnu11', opt, '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'),
                        str(ROOT / 'tests/mirror_budget_test.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
