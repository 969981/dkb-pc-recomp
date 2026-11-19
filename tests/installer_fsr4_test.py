"""installer/fsr4.py: AMD FSR 4's download, against a local HTTP server with stand-in files (no network, no AMD file).

The pinned table (LICENSE, FILES) is replaced by stand-ins with their own sizes and hashes. Checked: the license text
comes back only with its pinned hash; fetch() puts every file in place and installed() then holds; a file already in
place with its hash is not fetched again; a file whose bytes differ, or that is longer than pinned, is refused and
leaves no file and no .part behind; a server that does not answer, a reply cut short (IncompleteRead) and a
refused rename raise FetchError, not an OSError or an HTTPException.
"""
import hashlib
import http.server
import shutil
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from installer import fsr4  # noqa: E402

BLOBS = {'docs/license.md': b'Stand-in license: MIT for the listed files.\n' * 40,
         'bin/loader.dll': b'MZ loader' + bytes(range(256)) * 50,
         'bin/upscaler.dll': b'MZ upscaler' + bytes(255 - i for i in range(256)) * 9000}


def entry(path, name, data=None):
    data = BLOBS[path] if data is None else data
    return (path, name, len(data), hashlib.sha256(data).hexdigest())


class Handler(http.server.BaseHTTPRequestHandler):
    served = {}
    hits = []

    def do_GET(self):
        path = self.path.split('/', 2)[-1]
        Handler.hits.append(path)
        data = Handler.served.get(path)
        if data is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


class Fsr4Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.repo = f'http://127.0.0.1:{cls.server.server_port}/tag/'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()

    def setUp(self):
        Handler.served, Handler.hits = dict(BLOBS), []
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.pins = mock.patch.multiple(fsr4, LICENSE=entry('docs/license.md', 'LICENSE-amd.md'),
                                        FILES=(entry('bin/loader.dll', 'loader.dll'),
                                               entry('bin/upscaler.dll', 'upscaler.dll')))
        self.pins.start()
        self.addCleanup(self.pins.stop)

    def test_license(self):
        self.assertEqual(fsr4.license_text(self.repo), BLOBS['docs/license.md'].decode())
        Handler.served['docs/license.md'] = BLOBS['docs/license.md'].replace(b'MIT', b'XYZ')
        with self.assertRaises(fsr4.FetchError):
            fsr4.license_text(self.repo)

    def test_fetch_and_keep(self):
        self.assertFalse(fsr4.installed(self.dir))
        seen = []
        fsr4.fetch(self.dir, seen.append, repo=self.repo)
        self.assertTrue(fsr4.installed(self.dir))
        self.assertEqual(seen[-1], fsr4.total_bytes())
        self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ['LICENSE-amd.md', 'loader.dll', 'upscaler.dll'])
        Handler.hits = []
        fsr4.fetch(self.dir, repo=self.repo)   # all in place with their hashes: nothing fetched
        self.assertEqual(Handler.hits, [])
        (self.dir / 'loader.dll').write_bytes(b'changed')
        fsr4.fetch(self.dir, repo=self.repo)
        self.assertEqual(Handler.hits, ['bin/loader.dll'])
        self.assertTrue(fsr4.installed(self.dir))

    def test_wrong_bytes_refused(self):
        Handler.served['bin/upscaler.dll'] = BLOBS['bin/upscaler.dll'][:-1] + b'!'
        with self.assertRaises(fsr4.FetchError) as caught:
            fsr4.fetch(self.dir, repo=self.repo)
        self.assertEqual(caught.exception.args[0], 'upscaler.dll')
        self.assertFalse((self.dir / 'upscaler.dll').exists())
        self.assertFalse((self.dir / 'upscaler.dll.part').exists())
        self.assertFalse(fsr4.installed(self.dir))

    def test_too_long_refused(self):
        Handler.served['bin/loader.dll'] = BLOBS['bin/loader.dll'] + b'x' * 10
        with self.assertRaises(fsr4.FetchError):
            fsr4.fetch(self.dir, repo=self.repo)
        self.assertFalse((self.dir / 'loader.dll').exists())
        self.assertFalse((self.dir / 'loader.dll.part').exists())

    def test_missing_and_offline(self):
        del Handler.served['bin/loader.dll']
        with self.assertRaises(fsr4.FetchError):
            fsr4.fetch(self.dir, repo=self.repo)
        with self.assertRaises(fsr4.FetchError):
            fsr4.license_text('http://127.0.0.1:9/')

    def test_cut_reply_and_failed_rename_warn(self):
        # A reply cut short (http.client.IncompleteRead, an HTTPException, not an OSError) and a rename refused (the old
        # file in use) must come back as FetchError, which the step turns into a warning, never escape it.
        import http.client
        class Cut:
            def __enter__(self):
                return self
            def __exit__(self, *a):
                return False
            def read(self, n=-1):
                raise http.client.IncompleteRead(b'partial', 100)
        with mock.patch.object(fsr4.urllib.request, 'urlopen', return_value=Cut()):
            with self.assertRaises(fsr4.FetchError):
                fsr4.license_text(self.repo)
            with self.assertRaises(fsr4.FetchError):
                fsr4.fetch(self.dir, repo=self.repo)
        self.assertEqual([p.name for p in self.dir.iterdir()], [])
        with mock.patch.object(fsr4.os, 'replace', side_effect=PermissionError('in use')):
            with self.assertRaises(fsr4.FetchError) as caught:
                fsr4.fetch(self.dir, repo=self.repo)
        self.assertIn('in use', caught.exception.args[1])
        self.assertEqual([p.name for p in self.dir.iterdir()], [])   # no .part left

    def test_pinned_release(self):
        self.pins.stop()   # the real table: one release, AMD's repository at its tag, every file with a hash
        self.assertTrue(fsr4.REPO.startswith('https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/'))
        self.assertIn(fsr4.TAG, fsr4.REPO)
        names = [name for _, name, _, _ in fsr4.FILES]
        self.assertEqual(names, ['amd_fidelityfx_loader_vk.dll', 'amd_fidelityfx_upscaler_vk.dll'])
        for _, _, size, sha in (fsr4.LICENSE, *fsr4.FILES):
            self.assertGreater(size, 0)
            self.assertEqual(len(sha), 64)
        self.pins.start()


if __name__ == '__main__':
    unittest.main()
