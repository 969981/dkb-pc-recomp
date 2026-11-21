"""Compile and run Switch 2 controller radial stick calibration tests (runtime/switch2_proto.h) at -O0 and -O2.

No SDL, Bluetooth, or game files required.
Usage: python tests/switch2_stick_test.py --cc <clang>
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang') or 'clang')
    ap.add_argument('--source-root', type=Path, default=ROOT)
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix='dkb-switch2-stick-') as tmp:
        tmp_dir = Path(tmp)
        for opt in ('-O0', '-O2'):
            exe = tmp_dir / ('switch2_stick' + opt[2:] + ('.exe' if os.name == 'nt' else ''))
            subprocess.run([
                args.cc, '-std=gnu11', opt, '-Wall', '-Wextra', '-Werror',
                '-I', str(args.source_root / 'runtime'),
                str(args.source_root / 'tests' / 'switch2_stick_test.c'),
                '-lm', '-o', str(exe),
            ], check=True)
            res = subprocess.run([str(exe)], capture_output=True, text=True)
            if res.returncode != 0:
                if res.stdout:
                    sys.stderr.write(res.stdout)
                if res.stderr:
                    sys.stderr.write(res.stderr)
                return res.returncode
            if res.stdout:
                print(opt, res.stdout.strip())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
