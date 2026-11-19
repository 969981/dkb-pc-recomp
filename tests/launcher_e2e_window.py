"""Stand-in for a version's window in the end-to-end test: parses the arguments installer.update.start_version passes
and runs installer.gui.start_launcher, the code a real window runs before it opens (confirm_start, then the launcher
exe's replacement). E2E_KILL_BEFORE=current makes it die between writing previous.txt and current.txt."""
import argparse
import os
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, os.environ['E2E_REPO'])
from installer import gui, update  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('-m')
ap.add_argument('--base', type=Path)
ap.add_argument('--launcher', type=Path)
ap.add_argument('--after-update', action='store_true')
ap.add_argument('--allow-downgrade', action='store_true')
ap.add_argument('--lang')
args = ap.parse_args()
root = Path.cwd()
layout = SimpleNamespace(root=root, release=(root / 'VERSION').read_text(encoding='utf-8').strip())

kill = os.environ.get('E2E_KILL_BEFORE')
if kill:
    real = update.write_pointer

    def write_pointer(base, name, tag):
        if name == kill:
            os._exit(9)   # the process ends here, as a power cut or a kill would end it
        real(base, name, tag)
    update.write_pointer = write_pointer

gui.start_launcher(args, layout)
