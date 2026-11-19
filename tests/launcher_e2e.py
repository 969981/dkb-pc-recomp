"""End-to-end test of the Bananza launcher against a fake release server on 127.0.0.1 (Windows, MSYS2 clang).

    .venv\\Scripts\\python tests\\launcher_e2e.py [--cc CLANG] [--keep]

The C launcher (a -DDKB_LAUNCHER_TEST build, no windows) does the first install of release A for real: manifest,
download, SHA-256, tar.exe, current.txt, start of A's python\\pythonw.exe (a stub that logs its command line), with a
junk winhttp.dll and bcrypt.dll beside it that Windows must not load. The update to B then runs the window's own code:
an installer.gui.App on a withdrawn Tk root, set up as launcher.c starts A's window, whose release check, Update
button (App.start_update, update_worker, hand_over) and Go back run as a click runs them, on Tk's main loop. Its
requests go through installer.update.http_open with GitHub's https links mapped onto the local server (update.OPENER);
TLS and GitHub's redirects are not covered. B's python\\pythonw.exe is the same stub, which then forwards to
tests/launcher_e2e_window.py, which runs installer.gui.start_launcher (confirm_start and the launcher exe's
replacement) as B's window does before it opens. Scenarios: a tampered zip and a cancel at the first install, the
first install and a second start, two first installs at once, then from installed A: a tampered update, a window that
never confirms, a kill between previous.txt and current.txt, the update to B with the launcher's replacement, a second
start, and going back to A. Everything happens in a temporary folder (kept with --keep); exit code 0 when every check
passes, 77 (skipped) off Windows or without clang.
"""
import argparse
import dataclasses
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile
from pathlib import Path
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO))
from installer import update  # noqa: E402

WORK = Path(tempfile.mkdtemp(prefix='dkb-launcher-e2e-'))
RESULTS = []


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(f'{"PASS" if ok else "FAIL"}  {name}{"  -- " + str(detail) if detail and not ok else ""}', flush=True)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# ---- builds -----------------------------------------------------------------------------------------------------

def build_launcher(cc):
    sys.path.insert(0, str(REPO / 'tools'))
    import make_release
    inc = WORK / 'inc'
    make_release.launcher_header(inc / 'launcher_text.h', 'dkb')
    exe = WORK / 'launcher-test.exe'
    src = [str(s) for s in make_release.launcher_sources('dkb')]
    subprocess.run([cc, '-O2', '-mwindows', '-municode', '-fuse-ld=lld',
                    f'-Wl,--dependent-load-flag={make_release.DEPENDENT_LOAD_FLAG}', '-DDKB_LAUNCHER_TEST', '-I', str(inc),
                    *src, '-o', str(exe)], check=True)
    make_release.check_launcher(exe)
    return exe.read_bytes()


class Release:
    def __init__(self, folder, numbers, commit, launcher_bytes):
        self.version, self.tag = f'{numbers}+{commit}', f'v{numbers}'
        self.folder = folder
        folder.mkdir(parents=True)
        self.launcher_bytes = launcher_bytes
        self.zip = folder / f'DKB-Setup-{self.version}-windows.zip'
        with zipfile.ZipFile(self.zip, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('DKB-Setup/VERSION', self.version + '\n')
            z.write(WORK / 'stub.exe', 'DKB-Setup/python/pythonw.exe')
            z.writestr('DKB-Setup/installer/gui.py', '# the window\n')
            z.writestr('DKB-Setup/DKB-Setup.exe', launcher_bytes)
            z.writestr('DKB-Setup/llvm/bin/deep/file.txt', 'x' * 100000)
        self.launcher = folder / 'DKB-Launcher.exe'
        self.launcher.write_bytes(launcher_bytes)
        self.manifest_text = update.write_manifest(self.version, self.tag, self.zip, 'DKB-Setup', self.launcher)
        self.manifest = update.parse_manifest(self.manifest_text)


# ---- the fake GitHub --------------------------------------------------------------------------------------------

class Server:
    def __init__(self):
        self.files, self.requests, self.agents, self.delay = {}, [], set(), 0.0
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                server.requests.append(self.path)
                server.agents.add(self.headers.get('User-Agent'))
                data = server.files.get(self.path)
                if data is None:
                    self.send_response(404)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                for i in range(0, len(data), 16384):
                    if server.delay:
                        time.sleep(server.delay)
                    try:
                        self.wfile.write(data[i:i + 16384])
                    except OSError:
                        return
        self.httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def url(self, path):
        return f'http://127.0.0.1:{self.port}{path}'

    def publish(self, release, tamper=False):
        """release as GitHub's latest: the manifest at the latest link, the files at the tag's links, the API answer"""
        data = release.zip.read_bytes()
        if tamper:
            data = data[:-200] + bytes(b ^ 0x55 for b in data[-200:])
        name = urllib.request.quote(release.zip.name, safe='')
        self.files = {
            '/latest/dkb-release.txt': release.manifest_text.encode(),
            f'/download/{release.tag}/{update.MANIFEST}': release.manifest_text.encode(),
            f'/download/{release.tag}/{name}': data,
            f'/download/{release.tag}/DKB-Launcher.exe': release.launcher_bytes,
            '/api/latest': json.dumps({
                'tag_name': release.tag, 'html_url': f'https://github.com/{update.REPO}/releases/tag/{release.tag}',
                'body': 'notes', 'draft': False, 'prerelease': False,
                'assets': [{'name': update.MANIFEST, 'size': len(release.manifest_text)},
                           {'name': release.zip.name, 'size': release.zip.stat().st_size,
                            'digest': 'sha256:' + sha(release.zip)},
                           {'name': 'DKB-Launcher.exe', 'size': len(release.launcher_bytes)}]}).encode()}

    def open(self, request, timeout=None):
        """update.OPENER's open(): GitHub's https links of http_open's request mapped onto this server, with its
        headers (the generic User-Agent) passed on"""
        url = request.full_url
        for prefix, local in ((update.API, '/api/latest'), (update.LATEST_MANIFEST, '/latest/dkb-release.txt'),
                              (update.DOWNLOAD, '/download/')):
            if url.startswith(prefix):
                mapped = urllib.request.Request(self.url(local + url[len(prefix):]), headers=dict(request.header_items()))
                return urllib.request.urlopen(mapped, timeout=timeout)
        raise OSError(f'not mapped: {url}')


# ---- the player's folder ----------------------------------------------------------------------------------------

class Player:
    def __init__(self, name, launcher_bytes, server):
        self.dir = WORK / name
        self.dir.mkdir(parents=True)
        self.exe = self.dir / 'DKB-Launcher.exe'
        self.exe.write_bytes(launcher_bytes)
        self.base = self.dir / 'DKB-Recomp'
        self.log, self.stub_log = self.dir / 'launcher-messages.txt', self.dir / 'started.txt'
        self.server = server

    def env(self, **extra):
        return {**os.environ, 'DKB_LAUNCHER_LOG': str(self.log), 'STUB_LOG': str(self.stub_log),
                'DKB_LAUNCHER_MANIFEST': self.server.url('/latest/dkb-release.txt'),
                'DKB_LAUNCHER_DOWNLOAD': self.server.url('/download/'), **extra}

    def run(self, wait=True, **extra):
        p = subprocess.Popen([str(self.exe)], cwd=str(self.dir), env=self.env(**extra))
        if wait:
            p.wait(timeout=180)
        return p

    def messages(self):
        return self.log.read_text(encoding='utf-8').splitlines() if self.log.exists() else []

    def started(self, at_least=0):
        """the stub's log lines; waits up to 15 s for at_least of them (the launcher exits before its child logs)"""
        end = time.monotonic() + 15
        while True:
            lines = self.stub_log.read_text(encoding='utf-8').splitlines() if self.stub_log.exists() else []
            if len(lines) >= at_least or time.monotonic() > end:
                time.sleep(0.3)   # and lets the stub process end, so its exe can be deleted
                return self.stub_log.read_text(encoding='utf-8').splitlines() if self.stub_log.exists() else []
            time.sleep(0.1)

    def pointer(self, name):
        return update.read_pointer(self.base, name)

    def versions(self):
        v = self.base / 'versions'
        return sorted(p.name for p in v.iterdir()) if v.exists() else []

    def downloads(self):
        d = self.base / 'downloads'
        return sorted(p.name for p in d.iterdir()) if d.exists() else []


# ---- the window -------------------------------------------------------------------------------------------------

class Window:
    """The window launcher.c starts for the version `running` (its folder, --launcher, --base): an installer.gui.App on
    a withdrawn Tk root, whose requests reach the local server through update.http_open. A version's python\\pythonw.exe
    that it starts forwards to tests/launcher_e2e_window.py (forward=False: the stub only logs and exits, a window that
    never confirms its start)."""

    def __init__(self, player, running, forward=True, **window_env):
        import tkinter as tk
        from installer import gui, layout as layouts
        from installer.guilogic import Launcher
        self.gui, self.tk = gui, tk
        forwarding = {'E2E_FORWARD_PYTHON': sys.executable, 'E2E_FORWARD_SCRIPT': str(HERE / 'launcher_e2e_window.py')}
        self.env = {'E2E_REPO': str(REPO), 'STUB_LOG': str(player.stub_log), **(forwarding if forward else {}),
                    **window_env}
        self.patches = [patch.object(update, 'OPENER', player.server), patch.dict(os.environ, self.env)]
        for p in self.patches:
            p.start()
        self.root = tk.Tk()
        self.root.withdraw()
        layout = dataclasses.replace(layouts.find(None), root=player.base / 'versions' / running.tag,
                                     release=running.version)
        self.app = gui.App(self.root, 'en', layout, game='bananza', check_updates=False,
                           launcher=Launcher(base=player.base, exe=player.exe))
        self.closed = False

    def loop(self, done, timeout=120):
        """Tk's main loop, as the window runs it (a worker's later() needs it), until done() or the window closed"""
        end = time.monotonic() + timeout
        pending = []

        def poll():
            if done() or time.monotonic() > end:
                self.root.quit()
            else:
                pending[:] = [self.root.after(50, poll)]
        pending.append(self.root.after(50, poll))
        self.root.mainloop()
        try:
            self.root.winfo_exists()
        except self.tk.TclError:
            self.closed = True
        try:   # a poll still queued when the window closed would run against a destroyed root
            self.root.after_cancel(pending[0])
        except (self.tk.TclError, IndexError):
            pass

    def check(self):
        self.app.check_now()
        self.loop(lambda: self.app.check_button.instate(['!disabled']))
        return self.app.offer

    def click(self, action):
        """Update or Go back, then the main loop until the worker is done (and the window closed when it handed over)"""
        with patch.object(self.gui.messagebox, 'askyesno', return_value=True):   # Go back's question
            action()
        self.loop(lambda: not self.app.update_running and self.app.update_failure is not None)
        return None if self.closed else self.app.update_failure

    def close(self):
        if not self.closed:
            try:
                self.root.destroy()
            except self.tk.TclError:
                pass
        for p in reversed(self.patches):
            p.stop()


def update_in_window(player, running, **window_kw):
    """The home screen's check and Update button: (the offer, the failure the update screen shows or None, whether
    the window closed)"""
    w = Window(player, running, **window_kw)
    try:
        offer = w.check()
        failure = w.click(w.app.start_update) if offer and w.app.update_offered else ('not offered', {})
        return offer, failure, w.closed
    finally:
        w.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default=r'C:\msys64\ucrt64\bin\clang.exe')
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    if os.name != 'nt':
        print('skipped: the launcher is a Windows program')
        return 77
    if not Path(args.cc).is_file():
        print(f'skipped: no {args.cc}')
        return 77
    os.environ['PATH'] = str(Path(args.cc).parent) + os.pathsep + os.environ.get('PATH', '')
    subprocess.run([args.cc, '-O1', '-mwindows', '-municode', str(HERE / 'launcher_e2e_stub.c'), '-o',
                    str(WORK / 'stub.exe')], check=True)
    launcher = build_launcher(args.cc)
    a = Release(WORK / 'release-a', '0.5.0', 'aaaaaaa', launcher + b'\0release A')   # an overlay: another exe hash
    b = Release(WORK / 'release-b', '0.6.0', 'bbbbbbb', launcher + b'\0release B')
    server = Server()

    # -- first install, failure paths
    server.publish(a, tamper=True)
    p = Player('tampered-first', a.launcher_bytes, server)
    p.run()
    check('first install: a tampered zip is refused with the checksum message',
          any('checksum' in m for m in p.messages()), p.messages())
    check('first install: a tampered zip leaves no version, pointer, download or start',
          p.versions() == [] and p.downloads() == [] and p.started() == [], (p.versions(), p.downloads(), p.started()))

    server.publish(a)
    p = Player('cancelled-first', a.launcher_bytes, server)
    p.run(DKB_LAUNCHER_CANCEL='unpack')
    check('first install: a cancel after the download unpacks, keeps and starts nothing',
          p.versions() == [] and p.downloads() == [] and p.started() == [] and p.messages() == [p.messages()[0]],
          (p.versions(), p.downloads(), p.started(), p.messages()))

    # -- first install of A, from a Downloads folder that also holds a winhttp.dll and a bcrypt.dll
    p = Player('player', a.launcher_bytes, server)
    for dll in ('winhttp.dll', 'bcrypt.dll'):   # not images: had Windows loaded either, the launcher would not start
        (p.dir / dll).write_bytes(b'not a DLL')
    before = len(server.requests)
    p.run()
    started = p.started(1)
    check('first install: versions\\v0.5.0 unpacked, current.txt names it, downloads empty',
          p.versions() == ['current.txt', 'v0.5.0'] and p.pointer('current') == 'v0.5.0' and p.downloads() == [],
          (p.versions(), p.downloads()))
    check('first install: the winhttp.dll and bcrypt.dll beside the launcher were not loaded', len(started) == 1, started)
    check('first install: only the confirmation was shown', len(p.messages()) == 1 and 'downloads them' in p.messages()[0],
          p.messages())
    check('first install: A started from its folder with --launcher and --base',
          len(started) == 1 and started[0].split('\t')[0] == str(p.base / 'versions' / 'v0.5.0') and
          f'--launcher "{p.exe}" --base "{p.base}"' in started[0], started)
    check('first install: the manifest and the zip were requested once each', len(server.requests) - before == 2,
          server.requests[before:])
    before = len(server.requests)
    p.run()
    check('second start of A: no request, A starts again', len(server.requests) == before and len(p.started(2)) == 2,
          server.requests[before:])

    # -- two first installs at once: the second waits for the first, then starts what it installed
    server.delay = 0.2
    q = Player('twice', a.launcher_bytes, server)
    before = len(server.requests)
    first, second = q.run(wait=False), None
    time.sleep(0.5)
    second = q.run(wait=False)
    first.wait(timeout=300), second.wait(timeout=300)
    server.delay = 0.0
    zips = [r for r in server.requests[before:] if r.endswith('.zip')]
    check('two launchers at once: one download, both start v0.5.0, one version folder',
          len(zips) == 1 and len(q.started(2)) == 2 and q.versions() == ['current.txt', 'v0.5.0'],
          (server.requests[before:], q.started(), q.versions(), q.messages()))

    # -- the update to B in A's window, failure paths first
    shutil.copytree(p.base, WORK / 'a-installed')   # every update scenario starts from this installed A
    exe_a = p.exe.read_bytes()

    def reset():
        shutil.rmtree(p.base)
        shutil.copytree(WORK / 'a-installed', p.base)
        p.exe.write_bytes(exe_a)

    server.publish(b, tamper=True)
    offer, failure, closed = update_in_window(p, a)
    check('update: B is offered in A\'s window', offer is not None and offer.tag == 'v0.6.0', offer)
    check('update: a tampered zip is refused (update.checksum) and the window stays',
          failure and failure[0] == 'update.checksum' and not closed, (failure, closed))
    check('update: after the tampered zip, A is current, nothing staged, downloads empty',
          p.pointer('current') == 'v0.5.0' and p.versions() == ['current.txt', 'v0.5.0'] and p.downloads() == [],
          (p.versions(), p.downloads()))

    server.publish(b)
    reset()
    n = len(p.started())
    offer, failure, closed = update_in_window(p, a, forward=False)   # B's window exits without confirming
    p.started(n + 1)
    check('update: a window that never confirms is reported, A stays current and the window stays',
          failure and failure[0] == 'ui.update_start_failed' and failure[1].get('current') == 'v0.5.0' and not closed and
          p.pointer('current') == 'v0.5.0' and p.pointer('previous') is None, (failure, closed))
    n = len(p.started())
    p.run()
    check('update: after the failed start the launcher starts A', len(p.started(n + 1)) == n + 1 and
          p.started()[-1].split('\t')[0].endswith('v0.5.0'), p.started()[-1:])

    reset()
    n = len(p.started())
    offer, failure, closed = update_in_window(p, a, E2E_KILL_BEFORE='current')
    p.started(n + 1)
    check('update: killed between previous.txt and current.txt: reported, A still current',
          failure and failure[0] == 'ui.update_start_failed' and p.pointer('current') == 'v0.5.0',
          (failure, p.pointer('current'), p.pointer('previous')))
    n = len(p.started())
    p.run()
    check('update: after that kill the launcher starts A', len(p.started(n + 1)) == n + 1 and
          p.started()[-1].split('\t')[0].endswith('v0.5.0'), p.started()[-1:])

    # -- the update to B
    reset()
    before, n = len(server.requests), len(p.started())
    offer, failure, closed = update_in_window(p, a)
    done = p.started(n + 1)   # the stub logs once B's start code has run
    check('update: B checked, downloaded, verified, unpacked; its window confirmed and A\'s window closed',
          failure is None and closed and update.complete(p.base / 'versions' / 'v0.6.0', b.version), (failure, closed))
    check('update: B\'s window was started with --after-update from its own folder',
          len(done) == n + 1 and done[-1].split('\t')[0].endswith('v0.6.0') and '--after-update' in done[-1], done[-1:])
    check('update: current.txt is B, previous.txt is A, both folders kept, downloads empty',
          p.pointer('current') == 'v0.6.0' and p.pointer('previous') == 'v0.5.0' and
          p.versions() == ['current.txt', 'previous.txt', 'v0.5.0', 'v0.6.0'] and p.downloads() == [],
          (p.versions(), p.downloads()))
    check('update: the launcher exe replaced by B\'s, with no .old or .new left',
          sha(p.exe) == hashlib.sha256(b.launcher_bytes).hexdigest() and sorted(x.name for x in p.dir.iterdir()
                                                                                if x.name.startswith('DKB-Launcher')) == ['DKB-Launcher.exe'],
          sorted(x.name for x in p.dir.iterdir()))
    check('update: requests were the API, the manifest and the zip, with the generic User-Agent',
          len(server.requests) - before == 3 and update.USER_AGENT in server.agents, server.requests[before:])
    log = (p.base / 'logs' / 'launcher.log').read_text(encoding='utf-8')
    check('update: launcher.log has the check, the download, the unpack and the confirmed start',
          all(s in log for s in ('check: latest v0.6.0', 'downloaded and checked', 'unpacked into',
                                 'v0.6.0: its window started', 'v0.6.0 (0.6.0+bbbbbbb) started and is now current')), log)
    before, n = len(server.requests), len(p.started())
    p.run()   # now B's launcher binary
    check('second start after the update: B starts, no request', len(server.requests) == before and
          len(p.started(n + 1)) == n + 1 and p.started()[-1].split('\t')[0].endswith('v0.6.0'), p.started()[-1:])
    w = Window(p, b)
    try:
        offer = w.check()
        check('the check in B\'s window: B is the latest, no Update button',
              offer.direction == 0 and not w.app.update_offered, offer)
        # -- Go back to A (the home screen's button)
        before, n = len(server.requests), len(p.started())
        back = w.app.previous_tag()
        failure = w.click(w.app.go_back)
        closed = w.closed
    finally:
        w.close()
    p.started(n + 1)
    check('go back: A confirms, current.txt A, previous.txt B, both kept, A\'s launcher back, no download',
          back == 'v0.5.0' and failure is None and closed and p.pointer('current') == 'v0.5.0' and
          p.pointer('previous') == 'v0.6.0' and p.versions() == ['current.txt', 'previous.txt', 'v0.5.0', 'v0.6.0'] and
          sha(p.exe) == hashlib.sha256(a.launcher_bytes).hexdigest() and
          not [r for r in server.requests[before:] if r.endswith('.zip')], (back, failure, closed, p.versions()))
    with patch.object(update, 'OPENER', server):
        offer = update.check(a.version)
    folder = update.stage(Path('unused.zip'), p.base, offer.manifest)   # B's folder is complete: reused, not unpacked
    check('go back: B is offered again and its folder is reused without a download',
          update.may_install(offer) and folder == p.base / 'versions' / 'v0.6.0')

    failed = [name for name, ok in RESULTS if not ok]
    print(f'\n{len(RESULTS) - len(failed)} of {len(RESULTS)} passed')
    return 1 if failed else 0


if __name__ == '__main__':
    try:
        code = main()
    finally:
        if '--keep' in sys.argv:
            print(f'kept {WORK}')
        else:
            shutil.rmtree(WORK, ignore_errors=True)
    sys.exit(code)
