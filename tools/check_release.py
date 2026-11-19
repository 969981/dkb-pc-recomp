"""The release workflow's last check before it publishes (.github/workflows/dkb-release.yml, publish job).

    python3 tools/check_release.py DIST TAG

DIST holds the files the release will carry, TAG is the pushed tag. dkb-release.txt must read as the launcher reads it
(installer/update.py parse_manifest), name the tag's release and describe the published bytes: each file it lists is
in DIST with its size and SHA-256. A release tag (v0.4.2, installer/update.py TAG_RE) is published as the latest
release. A pre-release tag (v0.4.2-rc1, v0.4.2-beta.2: a release tag, a dash and letters, digits or dots) is
published as a pre-release, which neither the launcher nor its window ever offers (update.check refuses it, and
GitHub's latest release skips it); its manifest names the release tag in front of the dash, since VERSION holds the
numbers alone. Prints "latest" or "prerelease"; exits 1 with the reason when the files disagree.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from installer import update  # noqa: E402

PRERELEASE_RE = re.compile(r'(v\d+(?:\.\d+){0,3})-[0-9A-Za-z.]+')


class ReleaseError(Exception):
    pass


def check(dist, tag):
    """'latest' or 'prerelease' for the release of tag with the files in dist; ReleaseError(why) when they disagree."""
    dist = Path(dist)
    if update.TAG_RE.fullmatch(tag):
        kind, want = 'latest', tag
    elif PRERELEASE_RE.fullmatch(tag) and update.TAG_RE.fullmatch(PRERELEASE_RE.fullmatch(tag).group(1)):
        kind, want = 'prerelease', PRERELEASE_RE.fullmatch(tag).group(1)
    else:
        raise ReleaseError(f'{tag} is neither a release tag (v0.4.2) nor a pre-release tag (v0.4.2-rc1)')
    try:
        manifest = update.parse_manifest((dist / update.MANIFEST).read_bytes())
    except OSError as e:
        raise ReleaseError(f'{update.MANIFEST}: {e}') from None
    except update.UpdateError as e:
        raise ReleaseError(f'{update.MANIFEST} is not one the launcher reads: {e.params.get("detail")}') from None
    if manifest.tag != want:
        raise ReleaseError(f'{update.MANIFEST} names {manifest.tag}, the tag is {tag}: VERSION and the tag disagree')
    for asset in (manifest.tools, manifest.launcher):
        path = dist / asset.name
        if not path.is_file():
            raise ReleaseError(f'{update.MANIFEST} names {asset.name}, which is not among the release files')
        if path.stat().st_size != asset.size:
            raise ReleaseError(f'{asset.name} is {path.stat().st_size} bytes, {update.MANIFEST} says {asset.size}')
        if update.sha256(path) != asset.sha256:
            raise ReleaseError(f'{asset.name} does not match the SHA-256 {update.MANIFEST} gives')
    return kind


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    try:
        print(check(args[0], args[1]))
    except ReleaseError as e:
        print(f'::error::{e}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
