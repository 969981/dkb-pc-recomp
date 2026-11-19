"""Releases on GitHub: the update notice, and the launcher's one-click update (docs/installer.md, "Launcher and updates").

The notice (CLI and window): newer_release() compares this release with the latest one. It never blocks or fails an
install: any problem returns None.

The launcher (installer/launcher/launcher.c starts the window with --base): every Windows release publishes
dkb-release.txt (tools/make_release.py write_manifest), the tools zip it names and DKB-Launcher.exe.

    check()            the latest release and its manifest, as an Offer (newer, the same or older than this release)
    download()         the zip to <name>.part, kept under its final name only when its size and SHA-256 match
    stage()            the zip unpacked into versions/<tag>.part, renamed to versions/<tag> once it is complete
    start_version()    the new version's window, started by the old one, which waits until it confirms its start
    confirm_start()    called by every window the launcher starts: its own tag becomes current.txt, the one before it
                       previous.txt. A version that never gets this far leaves the old one current
    replace_launcher() the launcher exe replaced by the new release's in one rename (a running one is renamed to .old
                       first)

The base folder (launcher.c makes it: DKB-Recomp beside DKB-Launcher.exe):

    launcher.json            the window's settings: the install folder, whether to look for updates at start
    versions/current.txt     the tag that starts; previous.txt the one before it, kept until the next update
    versions/<tag>/          an unpacked release zip; <tag>.part while it unpacks
    downloads/               the zip while it downloads, deleted once unpacked
    temp/                    the installer's work folder (--temp), shared by every version, so an update keeps the
                             generated code and objects the steps reuse

The only network requests: the GitHub API's latest release (or, when the API does not answer, the manifest from the
release's download link), then the manifest and, after the player clicks Update, the zip. No personal data, a
generic User-Agent. Off with --no-update-check, DKB_NO_UPDATE_CHECK=1 or launcher.json "check_updates": false.
"""
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

REPO = 'Lucas-blip-png/dkb-pc-recomp'
API = f'https://api.github.com/repos/{REPO}/releases/latest'
DOWNLOAD = f'https://github.com/{REPO}/releases/download/'
LATEST_MANIFEST = f'https://github.com/{REPO}/releases/latest/download/dkb-release.txt'
MANIFEST = 'dkb-release.txt'
FORMAT = 1
USER_AGENT = 'dkb-recomp-launcher'
TIMEOUT = 3             # the notice and the check at start: a few seconds at most
DOWNLOAD_TIMEOUT = 60   # a stalled download fails after a minute without data
MANIFEST_LIMIT = 64 << 10
TOOLS_LIMIT = 8 << 30
LAUNCHER_LIMIT = 16 << 20
LAUNCHER_EXE = 'DKB-Setup.exe'   # the launcher inside the release zip; DKB-Launcher.exe is the same file

# The lengths are the buffers of installer/launcher/manifest.h, so the two readers accept the same manifests
TAG_RE = re.compile(r'(?=.{2,31}\Z)v\d+(?:\.\d+){0,3}')
VERSION_RE = re.compile(r'(?=.{1,63}\Z)\d+(?:\.\d+){0,3}(?:\+[0-9A-Za-z.]+)?')
NAME_RE = re.compile(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,127}')
SHA_RE = re.compile(r'[0-9a-f]{64}')


class UpdateError(Exception):
    """id is a catalog id (installer/text/<lang>.json), params its values."""

    def __init__(self, id, **params):
        super().__init__(id)
        self.id, self.params = id, params


def disabled(flag=False):
    return flag or os.environ.get('DKB_NO_UPDATE_CHECK') == '1'


# ---- versions ----------------------------------------------------------------------------------------------------

def numbers(version):
    """(0, 2, 0) from '0.2.0+33f8bbc', 'v0.2.0' or '0.2.0-dev+abc'; None for anything else (a pre-release tag too)."""
    m = re.fullmatch(r'v?(\d+(?:\.\d+)*)(?:-dev)?(?:\+.*)?', str(version).strip())
    return tuple(int(n) for n in m.group(1).split('.')) if m else None


def compare(a, b):
    """-1, 0 or 1 as release a is older than, the same as or newer than b; None when either is no release version.
    Build metadata (+commit) does not count, and 0.4 equals 0.4.0."""
    x, y = numbers(a), numbers(b)
    if x is None or y is None:
        return None
    width = max(len(x), len(y))
    x, y = x + (0,) * (width - len(x)), y + (0,) * (width - len(y))
    return (x > y) - (x < y)


# ---- the notice --------------------------------------------------------------------------------------------------

class HttpsRedirects(urllib.request.HTTPRedirectHandler):
    """Follows a redirect only to another https link: the manifest is what the downloads are checked against, so no
    hop of it may travel in clear text."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.lower().startswith('https://'):
            raise UpdateError('update.offline', detail=f'refused a redirect to {newurl}')
        return super().redirect_request(req, fp, code, msg, headers, newurl)


OPENER = urllib.request.build_opener(HttpsRedirects)


def http_open(url, accept=None, timeout=TIMEOUT):
    """GET url over HTTPS (redirects followed, as GitHub's download links need); the response is a file object."""
    if not url.startswith('https://'):
        raise UpdateError('update.offline', detail=f'refused {url}')
    headers = {'User-Agent': USER_AGENT}
    if accept:
        headers['Accept'] = accept
    return OPENER.open(urllib.request.Request(url, headers=headers), timeout=timeout)


def read_limited(response, limit):
    data = response.read(limit + 1)
    if len(data) > limit:
        raise UpdateError('update.manifest_bad', detail='too large')
    return data


def fetch_latest(opener=http_open):
    """The latest release JSON from GitHub (unauthenticated), or None."""
    try:
        with opener(API, 'application/vnd.github+json') as r:
            return json.loads(read_limited(r, 4 << 20))
    except Exception:
        return None


def newer_release(current, fetch=fetch_latest):
    """(tag, url) of the latest release when it is newer than `current`, else None. `fetch` returns the API JSON."""
    try:
        info = fetch()
        tag, url = info['tag_name'], info['html_url']
        if info.get('prerelease') or info.get('draft') or '-' in tag:   # v0.3.0-rc1 is not a release to offer
            return None
        latest, mine = numbers(tag), numbers(current)
        return (tag, url) if latest and mine and latest > mine else None
    except Exception:
        return None


# ---- the manifest ------------------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Asset:
    sha256: str
    size: int
    name: str
    top: str = ''   # the zip's one top folder

    def url(self, tag):
        return release_url(tag, self.name)


def release_url(tag, name):
    """The download link of the file name of release tag ('+' of a version becomes %2B, as GitHub's own links have it)."""
    return DOWNLOAD + tag + '/' + urllib.parse.quote(name, safe='')


@dataclass(frozen=True)
class Manifest:
    version: str
    tag: str
    tools: Asset
    launcher: Asset


def parse_manifest(text):
    """dkb-release.txt as a Manifest; UpdateError('update.manifest_bad') for anything else, a cut-off file included.

        dkb-release 1
        version <the release's VERSION, 0.4.2+abc1234>
        tag v<its numbers>
        tools <sha256> <bytes> <zip name> <top folder in the zip>
        launcher <sha256> <bytes> <exe name>

    Lines with another first word are left for later formats."""
    def bad(detail):
        return UpdateError('update.manifest_bad', detail=detail)

    if isinstance(text, bytes):
        try:
            text = text.decode('ascii')
        except UnicodeDecodeError:
            raise bad('not ASCII') from None
    lines = text.replace('\r\n', '\n').split('\n')
    if not lines or lines[0].split() != ['dkb-release', str(FORMAT)]:
        raise bad('no dkb-release 1 header')
    fields = {}
    for line in lines[1:]:
        words = line.split()
        if not words:
            continue
        if words[0] in fields:
            raise bad(f'{words[0]} twice')
        fields[words[0]] = words[1:]
    for key, count in (('version', 1), ('tag', 1), ('tools', 4), ('launcher', 3)):
        if len(fields.get(key, ())) != count:
            raise bad(f'{key} missing or incomplete')
    version, tag = fields['version'][0], fields['tag'][0]
    if not VERSION_RE.fullmatch(version) or not TAG_RE.fullmatch(tag) or compare(version, tag) != 0:
        raise bad(f'version {version} and tag {tag}')

    def asset(words, suffix, limit):
        sha, size, name = words[0], words[1], words[2]
        top = words[3] if len(words) > 3 else ''
        if not SHA_RE.fullmatch(sha) or not size.isdigit() or not 0 < int(size) <= limit:
            raise bad(f'{name}: checksum or size')
        if not NAME_RE.fullmatch(name) or not name.lower().endswith(suffix) or (len(words) > 3 and not NAME_RE.fullmatch(top)):
            raise bad(f'{name}: name')
        return Asset(sha, int(size), name, top)

    return Manifest(version, tag, asset(fields['tools'], '.zip', TOOLS_LIMIT), asset(fields['launcher'], '.exe', LAUNCHER_LIMIT))


def write_manifest(version, tag, tools, top, launcher):
    """The text of dkb-release.txt for the files tools and launcher (tools/make_release.py)."""
    lines = [f'dkb-release {FORMAT}', f'version {version}', f'tag {tag}',
             f'tools {sha256(tools)} {tools.stat().st_size} {tools.name} {top}',
             f'launcher {sha256(launcher)} {launcher.stat().st_size} {launcher.name}']
    text = '\n'.join(lines) + '\n'
    parse_manifest(text)   # what we publish must be what the launcher accepts
    return text


# ---- the check ---------------------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Offer:
    tag: str
    version: str             # the manifest's, or the tag without a manifest
    direction: int           # 1 newer than this release, 0 the same, -1 older
    notes: str = ''          # the release notes, as GitHub has them
    page: str = ''           # the release page
    manifest: Manifest = None   # None: a release made before the launcher (only its page can be offered)


def check(current, opener=http_open):
    """The latest release beside `current`, as an Offer. UpdateError('update.offline') when GitHub could not be read,
    'update.manifest_bad' or 'update.manifest_mismatch' when the release's own files disagree."""
    info = fetch_latest(opener)
    if info is None:   # the API's limit (60 an hour) or its outage: the manifest from the release's download link
        try:
            with opener(LATEST_MANIFEST) as r:
                manifest = parse_manifest(read_limited(r, MANIFEST_LIMIT))
        except UpdateError as e:
            if e.id == 'update.manifest_bad':
                raise
            raise UpdateError('update.offline', detail=e.params.get('detail', '')) from None
        except Exception as e:
            raise UpdateError('update.offline', detail=str(e)) from None
        return Offer(manifest.tag, manifest.version, compare(manifest.version, current) or 0,
                     page=f'https://github.com/{REPO}/releases/tag/{manifest.tag}', manifest=manifest)
    try:
        tag = info['tag_name']
        assets = {a['name']: a for a in info.get('assets', [])}
        if info.get('draft') or info.get('prerelease') or not TAG_RE.fullmatch(tag):
            raise UpdateError('update.offline', detail=f'latest release {tag} is not a release to offer')
    except (KeyError, TypeError):
        raise UpdateError('update.offline', detail='unexpected answer') from None
    notes, page = str(info.get('body') or ''), str(info.get('html_url') or '')
    if MANIFEST not in assets:
        return Offer(tag, tag, compare(tag, current) or 0, notes, page)
    try:
        with opener(release_url(tag, MANIFEST)) as r:
            manifest = parse_manifest(read_limited(r, MANIFEST_LIMIT))
    except UpdateError:
        raise
    except Exception as e:
        raise UpdateError('update.offline', detail=str(e)) from None
    if manifest.tag != tag:
        raise UpdateError('update.manifest_mismatch', name=MANIFEST)
    for item in (manifest.tools, manifest.launcher):
        published = assets.get(item.name)
        digest = str((published or {}).get('digest') or '')
        if not published or published.get('size') != item.size or (digest.startswith('sha256:') and digest[7:] != item.sha256):
            raise UpdateError('update.manifest_mismatch', name=item.name)
    return Offer(tag, manifest.version, compare(manifest.version, current) or 0, notes, page, manifest)


def may_install(offer, allow_downgrade=False):
    """Whether the window offers the Update button for offer: a newer release, or an older one only when asked."""
    return offer.manifest is not None and (offer.direction > 0 or (allow_downgrade and offer.direction < 0))


# ---- download and unpack -----------------------------------------------------------------------------------------

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def download(asset, tag, folder, opener=http_open, progress=None, cancelled=None, tries=3, sleep=time.sleep):
    """asset of release tag into folder: written to <sha256>.zip.part and renamed to <sha256>.zip only when its size and
    SHA-256 are the manifest's. A file already there that checks out is used again. Returns its path."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    final = folder / f'{asset.sha256}{Path(asset.name).suffix}'
    if final.is_file() and final.stat().st_size == asset.size and sha256(final) == asset.sha256:
        return final
    part = final.with_name(final.name + '.part')
    last = None
    for attempt in range(tries):
        if attempt:
            sleep(3 << attempt)
        try:
            return fetch_to(asset, tag, part, final, opener, progress, cancelled)
        except UpdateError as e:
            if e.id != 'update.download_failed':   # a checksum, a size, a cancel: trying again changes nothing
                raise
            last = e
    raise last


def fetch_to(asset, tag, part, final, opener, progress, cancelled):
    h, have = hashlib.sha256(), 0
    try:
        with opener(asset.url(tag), 'application/octet-stream', DOWNLOAD_TIMEOUT) as r, open(part, 'wb') as out:
            while True:
                if cancelled and cancelled():
                    raise UpdateError('update.cancelled')
                block = r.read(1 << 20)
                if not block:
                    break
                have += len(block)
                if have > asset.size:
                    raise UpdateError('update.download_size', name=asset.name, size=asset.size)
                h.update(block)
                out.write(block)
                if progress:
                    progress(have, asset.size)
            out.flush()
            os.fsync(out.fileno())
        if have != asset.size:
            raise UpdateError('update.download_failed', name=asset.name,
                              detail=f'{have} of {asset.size} bytes')
        if h.hexdigest() != asset.sha256:
            raise UpdateError('update.checksum', name=asset.name)
        os.replace(part, final)
        return final
    except UpdateError:
        part.unlink(missing_ok=True)
        raise
    except (OSError, ValueError) as e:   # urllib's URLError and the timeouts are OSErrors
        part.unlink(missing_ok=True)
        raise UpdateError('update.download_failed', name=asset.name, detail=str(e)) from None


def version_python(folder):
    return Path(folder) / 'python' / ('pythonw.exe' if os.name == 'nt' else 'bin/python3')


def complete(folder, version):
    """folder is an unpacked release of version: its VERSION, its Python, the installer and the launcher are there."""
    folder = Path(folder)
    try:
        if (folder / 'VERSION').read_text(encoding='utf-8').strip() != version:
            return False
    except OSError:
        return False
    need = [version_python(folder), folder / 'installer' / 'gui.py']
    if os.name == 'nt':
        need.append(folder / LAUNCHER_EXE)
    return all(p.is_file() for p in need)


def is_link(path):
    """A symbolic link or a Windows junction (a mount point, which is_symlink() does not count): removed as itself,
    never entered."""
    path = Path(path)
    return path.is_symlink() or bool(getattr(path, 'is_junction', lambda: False)())


def remove_tree(path):
    """path and what it holds, without following links; False when something is in use (it goes at a later run). A
    link or junction, path itself included, is removed as a link: what it points to stays."""
    path = Path(path)
    if not os.path.lexists(path):
        return True
    if is_link(path) or path.is_file():   # unlink removes a directory link or junction too, not its target
        try:
            path.unlink()
            return True
        except OSError:
            return False
    trash = path.with_name(path.name + '.trash') if not path.name.endswith('.trash') else path
    try:
        if trash != path:
            remove_tree(trash)
            path.rename(trash)   # a folder in use cannot be renamed: it stays whole instead of half deleted
    except OSError:
        return False

    def writable(func, p, _exc):   # a read-only file (git objects, some tools) is made writable and tried again
        try:
            os.chmod(p, stat.S_IWRITE)
            func(p)
        except OSError:
            pass

    try:
        if sys.version_info >= (3, 12):
            shutil.rmtree(trash, onexc=writable)
        else:   # pragma: no cover
            shutil.rmtree(trash, onerror=writable)
    except OSError:
        pass
    return not trash.exists()


WINDOWS_DEVICES = {'CON', 'PRN', 'AUX', 'NUL', 'CONIN$', 'CONOUT$', *(f'{d}{n}' for d in ('COM', 'LPT')
                                                                    for n in (*'123456789', '¹', '²', '³'))}
UNPACK_RATIO = 20           # unpack_limit: at most 20 times the zip's size (0.4.1's tools zip unpacks to 3.6 times)
UNPACK_FLOOR = 1 << 30      # but always 1 GiB, so that a small zip is never refused for its ratio
UNPACK_LIMIT = 64 << 30     # and never more than 64 GiB


def unpack_limit(asset):
    """The most a zip of asset may unpack to: UNPACK_RATIO times its checked size, at least UNPACK_FLOOR, at most
    UNPACK_LIMIT. A zip whose members add up to more (a zip bomb) is refused before anything is written."""
    return min(UNPACK_LIMIT, max(UNPACK_FLOOR, UNPACK_RATIO * asset.size))


def windows_name_ok(part):
    """Whether Windows creates a file of exactly this name: no character it forbids (<>:"|?* and controls), no trailing
    dot or space (Windows drops them, so two names could meet), no device name (CON, NUL.txt, COM1...)."""
    if any(c in '<>:"|?*' or ord(c) < 32 for c in part) or part != part.rstrip(' .'):
        return False
    return part.split('.')[0].rstrip(' ').upper() not in WINDOWS_DEVICES


def member_path(name, top):
    """The path below the top folder of a zip member, or UpdateError for a name that could leave the folder or that
    Windows would write under another name."""
    if '\\' in name or name.startswith('/') or ':' in name:
        raise UpdateError('update.zip_bad', name=name)
    parts = PurePosixPath(name).parts
    if not parts or parts[0] != top or any(p in ('', '.', '..') or not windows_name_ok(p) for p in parts):
        raise UpdateError('update.zip_bad', name=name)
    return PurePosixPath(*parts[1:]) if len(parts) > 1 else None


def stage(zip_path, base, manifest):
    """The checked zip unpacked into versions/<tag>: first into <tag>.part, renamed once complete. A failure removes
    the .part folder and leaves every other version as it was. Returns the version folder."""
    versions = Path(base) / 'versions'
    target, part = versions / manifest.tag, versions / (manifest.tag + '.part')
    if complete(target, manifest.version):
        return target
    versions.mkdir(parents=True, exist_ok=True)
    if not remove_tree(part):
        raise UpdateError('update.stage_failed', path=str(part), detail='in use')
    try:
        part.mkdir()
        with zipfile.ZipFile(zip_path) as z:
            limit = unpack_limit(manifest.tools)
            if sum(info.file_size for info in z.infolist()) > limit:   # zipfile writes no more than file_size each
                raise UpdateError('update.zip_large', size=limit)
            for info in z.infolist():
                rel = member_path(info.filename, manifest.tools.top)
                if rel is None:
                    continue
                if stat.S_ISLNK(info.external_attr >> 16):
                    raise UpdateError('update.zip_bad', name=info.filename)
                out = part.joinpath(*rel.parts)
                if info.is_dir():
                    out.mkdir(parents=True, exist_ok=True)
                    continue
                out.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as src, open(out, 'wb') as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
        if not complete(part, manifest.version):
            raise UpdateError('update.stage_failed', path=str(part), detail='incomplete')
        if target.exists() and not remove_tree(target):
            raise UpdateError('update.stage_failed', path=str(target), detail='in use')
        os.replace(part, target)
        return target
    except UpdateError:
        remove_tree(part)
        raise
    except (OSError, zipfile.BadZipFile, ValueError) as e:
        remove_tree(part)
        raise UpdateError('update.stage_failed', path=str(part), detail=str(e)) from None


# ---- which version starts ----------------------------------------------------------------------------------------

def read_pointer(base, name):
    """The tag in versions/<name>.txt, or None (no file, or not a tag)."""
    try:
        tag = (Path(base) / 'versions' / f'{name}.txt').read_text(encoding='ascii').strip()
    except (OSError, UnicodeDecodeError):
        return None
    return tag if TAG_RE.fullmatch(tag) else None


def write_pointer(base, name, tag):
    path = Path(base) / 'versions' / f'{name}.txt'
    tmp = path.with_name(path.name + '.tmp')
    with open(tmp, 'w', encoding='ascii', newline='\n') as f:
        f.write(tag + '\n')
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def own_tag(base, root):
    """The tag of the version folder root when it is one of base's versions, else None (a checkout, the plain zip)."""
    root, versions = Path(root).resolve(), (Path(base) / 'versions').resolve()
    return root.name if root.parent == versions and TAG_RE.fullmatch(root.name) else None


def confirm_start(base, tag):
    """tag's window is up: it becomes current.txt and the version before it previous.txt. Older versions go (one in
    use stays and goes at a later start). True when current changed."""
    current = read_pointer(base, 'current')
    if current == tag:
        return False
    if current and (Path(base) / 'versions' / current).is_dir():
        write_pointer(base, 'previous', current)
    write_pointer(base, 'current', tag)
    prune(base)
    return True


def prune(base):
    """Remove every version folder but current and previous, leftovers of a stopped unpack, and every file left in
    downloads/ (a download cut off by a closed window or a stop: an update deletes its zip once unpacked, so by the
    time a new version confirms its start nothing there is still wanted). A file in use stays. Never fails."""
    versions = Path(base) / 'versions'
    keep = {read_pointer(base, 'current'), read_pointer(base, 'previous')} - {None}
    for folder, wanted in ((versions, lambda p: p.is_dir() and p.name not in keep),
                           (Path(base) / 'downloads', lambda p: p.is_file() and not p.is_symlink())):
        try:
            entries = list(folder.iterdir())
        except OSError:
            continue
        for p in entries:
            if wanted(p):
                remove_tree(p)


def start_version(base, tag, launcher, extra=(), spawn=subprocess.Popen, timeout=60, sleep=time.sleep):
    """Start versions/<tag>'s window (--after-update), then wait until it confirms its start: 'started' when it did,
    'failed' when its process ended first (current.txt is still the old version), 'slow' when neither happened in
    timeout seconds."""
    folder = Path(base) / 'versions' / tag
    cmd = [str(version_python(folder)), '-m', 'installer.gui', '--base', str(base), '--after-update', *extra]
    if launcher:
        cmd[3:3] = ['--launcher', str(launcher)]
    proc = spawn(cmd, cwd=str(folder))
    waited = 0.0
    while waited < timeout:
        if read_pointer(base, 'current') == tag:
            return 'started'
        if proc.poll() is not None:
            return 'started' if read_pointer(base, 'current') == tag else 'failed'
        sleep(0.25)
        waited += 0.25
    return 'slow'


# ---- the launcher exe --------------------------------------------------------------------------------------------

def replace_launcher(launcher, new):
    """Put new (the launcher of the release that runs, checked with its zip) in place of the launcher exe the player
    started, when they differ. A copy of new, checked again, replaces it in one rename: launcher.c has exited by the
    time this window runs, so the exe is normally not running and the launcher name is never missing. A running exe
    (a second start of the launcher) cannot be overwritten on Windows but can be renamed: then it becomes <name>.old
    (deleted at a later start) and the copy takes its name. No .new copy is left behind. True when replaced."""
    launcher, new = Path(launcher), Path(new)
    if not launcher.is_file() or not new.is_file() or launcher.suffix.lower() != '.exe':
        return False
    want = sha256(new)
    if sha256(launcher) == want:
        return False
    staged = launcher.with_name(launcher.name + '.new')
    old = launcher.with_name(launcher.name + '.old')
    try:
        shutil.copyfile(new, staged)
        if sha256(staged) != want:
            raise UpdateError('update.checksum', name=new.name)
        try:
            os.replace(staged, launcher)
            return True
        except OSError:   # the exe runs (or something holds it): renamed away instead
            pass
        try:
            old.unlink(missing_ok=True)
        except OSError:   # an .old still running (the update before this one): its name stays taken
            return False
        os.replace(launcher, old)
        try:
            os.replace(staged, launcher)
        except OSError:
            os.replace(old, launcher)
            raise
        return True
    finally:
        try:
            staged.unlink(missing_ok=True)   # gone already once it took the launcher's name
        except OSError:
            pass


def clean_old_launcher(launcher):
    try:
        Path(str(launcher) + '.old').unlink(missing_ok=True)
    except OSError:
        pass
