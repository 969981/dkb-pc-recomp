# Automatic upstream synchronization and source releases

This repository follows [Lucas-blip-png/dkb-pc-recomp](https://github.com/Lucas-blip-png/dkb-pc-recomp), branch **`bananza`** (not `main`).

The GitHub Actions workflow is [`.github/workflows/upstream-sync-release.yml`](../.github/workflows/upstream-sync-release.yml).

## What it does

- Polls upstream `bananza` every 30 minutes, subject to GitHub Actions scheduling delays.
- Can also be started manually using **Actions → Sync DKB upstream and publish → Run workflow**.
- Fetches the upstream branch, compares its commit SHA and safely **merges** new commits into the local `bananza` branch, keeping this fork's CI automation and documentation.
- Runs the repository's game-content guard (including the staged-file guard before any merge commit) and its guard tests.
- Creates one **prerelease source snapshot** per upstream commit: `sync-<first 12 characters of upstream commit SHA>`.
- Skips a snapshot when the same upstream SHA has already been released. GitHub automatically attaches source .zip and .tar.gz links.
- Stops on merge conflicts without force pushing, resetting or deleting local changes.

The first run also publishes an initial source snapshot of the upstream revision already present in this fork.

## Why this does not create a Windows game executable

Upstream currently has **no GitHub Releases**, and its README explicitly states that the game is not playable, has no installer, and that each user must compile the executable from their own dump. The `dkb.exe` executable includes code generated from copyrighted game files and must **not** be publicly distributed.

This workflow therefore publishes **source-only** GitHub releases, not runnable installers or completed game builds. It intentionally does not upload Nintendo game content, console keys, derived code or a built game executable.

If upstream introduces a redistributable, game-content-free launcher or installer in the future, that should first be reviewed and tested before a separate binary publishing step is added.

## Validation and recovery

- [Workflow runs](../../actions/workflows/upstream-sync-release.yml)
- [Source releases](../../releases)
- GitHub Actions must be enabled and the built-in `GITHUB_TOKEN` must have `contents: write` permission (requested by this workflow).
- Merge conflicts will fail safely and require a manual fix followed by a rerun.
- Scheduled workflows can be delayed or occasionally skipped. To retry immediately, use `workflow_dispatch`.
- Source release tags identify **upstream** commit SHAs even when this fork has local commits. The release source archive is taken from the resulting local merged history.

## Building locally

Follow upstream's [building instructions](https://github.com/Lucas-blip-png/dkb-pc-recomp#building-from-source). Your own extracted game files remain on your PC and must not be uploaded to public CI or releases.
