"""Compiles and runs tests/switch_rumble_test.c (runtime/switch_rumble.c against SDL's rumble tables); no game data."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    ap.add_argument('--out', type=Path, default=ROOT / 'build/tests/switch-rumble')
    args = ap.parse_args()
    out = args.out.resolve()
    out.relative_to((ROOT / 'build').resolve())
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'switch_rumble_test.exe'
    rt = ROOT / 'runtime'
    command = [args.cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', rt,
               ROOT / 'tests/switch_rumble_test.c', rt / 'switch_rumble.c', '-o', exe, '-lm']
    compiled = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120)
    if compiled.returncode:
        print(compiled.stdout + compiled.stderr)
        return compiled.returncode
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    print(result.stdout + result.stderr, end='')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
