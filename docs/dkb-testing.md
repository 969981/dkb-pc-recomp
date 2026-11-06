# Testing DKB Recomp

This guide is for running DKB Recomp on your own PC and reporting what happens. You build it from source with your own
copy of the game. Nothing from the game is in this repository, and nothing you build from it may be shared: not the
executable, not the generated code, not the shader cache.

## What you need

- Windows 10 or 11 on an x86-64 CPU with AVX2.
- 16 GB of RAM. The game uses about 13 GB, so close other large programs first.
- A GPU with Vulkan 1.3 and a current driver.
- About 15 GB free for the build (the packed RomFS is about 9 GB), besides the space your dump takes.
- *Donkey Kong Bananza* base version 1.0.0, dumped from your own Switch 2: the ExeFS (`main`, `sdk`, `main.npdm`) and
  the RomFS as a folder. Update 458752 is not supported yet. This project does not explain how to dump the game.
  If your dump is a single `.nup` file (the container Eglantine and Eden load), `tools/extract_nup.py` writes both
  folders from its base package: `python tools\extract_nup.py <game>.nup --list` shows the packages, and
  `python tools\extract_nup.py <game>.nup --out build\extracted\dkb100-dump` writes `exefs` and `romfs` there, checking
  every file's SHA-256. Pass those two folders to the commands in the README.

## Build

Install the toolchain from [building.md](building.md) (MSYS2 UCRT64 Clang, Ninja, Python 3.12 in `.venv`, .NET SDK),
then run the commands in the README, "Building from source". The executable ends up in `build\dkb100\dkb.exe`.

The generation command must include `--patches recomp\patches-dkb100.txt`. This enables the controller setup patch
that bypasses the game's HID server proxy, which the host does not implement. If you generated the code without
that argument, rerun the README's generation command with it and then rebuild with `tools\build_game.py`.
Rebuilding alone leaves the unpatched generated code in place and the game stops during controller setup.

## Run

```
build\dkb100\dkb.exe
```

Run it from inside the repository: it finds the shader translator (`build\xlate\totk_xlate.exe`) by looking up from
its own folder. The first run translates every shader it meets and is slow; later runs use the cache.

Useful settings, as environment variables (`set NAME=value` in the same Command Prompt):

| Setting | Effect |
| --- | --- |
| `TOTK_PROFILE_DIR=<folder>` | Use this folder for the save and settings. A new, empty folder gives a fresh start, which is what reports should use. |
| `TOTK_GPU_NAME=<text>` | Use the first GPU whose name contains this text, on machines with more than one. |
| `TOTK_GPU=null` | Run without a picture (no Vulkan). Useful to tell CPU and OS bugs from graphics bugs. |
| `TOTK_XLATE_PROCS=<n>` | Shader translator processes at run time (default 2). Lower it if RAM runs out. |
| `TOTK_TRACE=perf` | Log the frame time, what holds frames back and the GPU time every 60 frames. For stalls and low frame rates. |
| `TOTK_PERF_SLOW_GPU_MS=<ms>` | With `TOTK_TRACE=perf`, a replay part that runs this long on the GPU (default 100) gets a line with its draw and dispatch counts and the NVN 2 names of its stages, a second line with the arguments of its indirect draws as read back afterwards, and a third saying whether a shader in that part wrote the largest one's arguments, and which. |
| `TOTK_FIX_INDIRECT_RESET=0` | Draws the terrain's indirect-record reset program as a shader instead of the host doing its stores (#48). The shader's stores are lost, so the records keep zero or stale contents; for comparisons only. |
| `TOTK_EXPERIMENTAL_GEOMETRY=1` | Replays draws with an ordinary geometry stage through it (#52). Off by default; other geometry draws are rejected once in the log. |
| `TOTK_NATIVE_GS=0` (or `DKB_NATIVE_GS=0`) | Turns off the replay of the game's native passthrough geometry stages on the portable path. That replay draws the destructible terrain (#45) and is on by default; with it off the terrain is black. The varyings are inferred, not read from the hardware skip mask. |
| `DKB_GEOMETRY_CONTEXT=1` | Logs the first 12 draws with a geometry stage and their buffers (#58). `DKB_GEOMETRY_CONTEXT_ORIGIN=<hex>` keeps only draws with that stage origin. |
| `TOTK_TRACE=codec` | Log each call of the video decoder that moves a buffer or a picture. For guide movies that stop or show a flat colour. |

The settings keep TotK Recomp's `TOTK_` names, so its fixes apply without a rename. Each one can also be written with
`DKB_` in its place (`DKB_GPU=null`); when both are set, the `DKB_` one wins.

Command-line options: `--seconds S` stops the run after S seconds of game time, and `--shot frame.png` with it saves
that frame. A shot is for your own debugging; do not post it if it shows game content.

## Experimental ultrawide mode

The optional `DKB_ULTRAWIDE=1` mode uses `DKB_OUTPUT_RES` for a wider native render
target and increases the camera's horizontal view while keeping its vertical
view. It is disabled by default. The HUD retains its proportions and can sit at
the screen edges or inside the central 16:9 area; menus remain centered.
Saving and loading indicators follow the same HUD setting. Their local capture
panes stay unchanged; only the final icon, shadow and loading group anchors move.
Scenes and layouts outside the recorded test coverage still need checking.
With no resolution setting, Bananza retains its stock 2880x1620 output.

Generate the matching background overlay from your own packed RomFS before
running. Install the Python `zstandard` package in the build environment if it
is not already present. For a build in `build/ultrawide`, for example:

```powershell
python tools/dkb_wide_layout.py --romfs build/extracted/dkb100-packed/romfs --out build/ultrawide/romfs-patches-3440x1440 --aspect 3440x1440
python tools/dkb_wide_layout.py --romfs build/extracted/dkb100-packed/romfs --out build/ultrawide/romfs-patches-3440x1440-hud-edges --aspect 3440x1440 --hud edges
$env:DKB_ULTRAWIDE = '1'
$env:DKB_OUTPUT_RES = '3440x1440'
$env:DKB_ASPECT = 'fit'
build/ultrawide/dkb.exe
```

The overlay belongs beside that build's `romfs-index.bin` and is selected only
while ultrawide is enabled. Its original-file and replacement hashes are checked
by the existing RomFS overlay loader. It contains game data and must stay private
under `build/`. Backgrounds are extended separately from logos, text, controls,
and guide movie panes. Regenerate it when changing the output resolution.

The title's cave wall is a particle effect rather than a layout pane. The overlay
uniformly enlarges the `BootMenuEnv` wall and atmosphere layers to cover the
viewport, cropping their top and bottom instead of distorting their proportions.
The logo, text, debris and animation timing stay unchanged. This requires
regenerating existing overlays; replacing the executable alone does not update
the backdrop. The generator validates both emitter names, bounds and original
scales before writing any replacement resources.

F1 opens the existing settings menu. On the Screen tab, **Ultrawide** enables
the wider camera view, **Render resolution** chooses the render target, and **HUD
position** selects **Screen edges** (the default) or **Centered (16:9)**. These
three settings apply after restarting because the game loads its display and
layout resources at boot.
Settings are saved in `totk.ini` beside the executable. Environment overrides
lock their corresponding menu controls; omit `DKB_ULTRAWIDE`, `DKB_OUTPUT_RES`
and `DKB_ASPECT` from a launcher when using the menu to save these choices.

Render resolution also works with Ultrawide off: choose a 16:9 size from 1280x720
through the original 2880x1620. With Ultrawide on, the list
offers 21:9 and 32:9 sizes. Switching Ultrawide selects a resolution near the
previous render height, preferring the monitor's aspect ratio when enabling it.
Window size changes the host window rather than the game's render resolution.

`DKB_HUD_POSITION=edges|centered` is the equivalent override. Edge mode loads
`romfs-patches-<width>x<height>-hud-edges`, a complete overlay including the
backgrounds. If that folder is absent, the centered overlay is used and the log
reports the fallback. Generate both folders for each installed menu resolution:
2560x1080, 3440x1440 and 3840x1080. The 32:9 menu preset is 3840x1080.
5120x1440 repeatedly crashed while creating game render resources during
gameplay loading, and 3840x2160 also failed. Menu presets are kept within the
largest tested frame, 3440x1440, pending support for larger render resources.
The wider parser range remains available for diagnostic environment overrides;
accepting a resolution in the parser does not establish game compatibility.

Picture fitting only preserves or stretches the finished image when
the window and game have different shapes. It does not change the camera view;
use Ultrawide for that. Keep Black bars selected for undistorted proportions.

`python tests/dkb_wide_layout_test.py` checks synthetic layouts and animation
keys without game data, including untouched panes, animation tangents and
fail-closed checks for an unexpected layout revision. It also checks that the
title-effect patch changes only the wall and atmosphere scale, and rejects
unknown or malformed emitter data.

`python tests/dkb_wide_projection_test.py` exercises the lifted UI patches on
full-screen and local capture matrices, including register and stack preservation.
The shared orthographic projection path widens only the game's 1920x1080 UI
canvas. Smaller capture canvases keep their original projection so dialogue
backgrounds, captured text and icon shadows are not narrowed a second time.

The configuration boundary test runs without game data:

```powershell
clang -O2 -march=x86-64-v3 -I runtime tests/dkb_wide_test.c -o build/dkb-wide-test.exe
build/dkb-wide-test.exe
build/ultrawide/dkb.exe --probe-cfg
```

Set `DKB_ULTRAWIDE=0` with a 16:9 `DKB_OUTPUT_RES` to use a standard view at the
selected resolution. Use `2880x1620` for the original render dimensions. The wide
background overlay is ignored at 16:9. Invalid resolutions, or a wide resolution
with Ultrawide off, fall back to stock 2880x1620 and produce a log message.
Keep `DKB_ASPECT=fit` for a centered picture
on a wider monitor. Neither mode changes your save files. The feature requires
regenerating with `recomp/patches-dkb100.txt`, just like the controller patch.

## What to report

Open an issue with the issue form, or comment on [#3](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/3)
(GPU testing) or [#7](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/7) (playing further):

- the commit you built (`git rev-parse --short HEAD`);
- GPU, driver version and Windows version;
- how far the game got and what you did just before it stopped;
- the log, `logs\totk.log` next to `dkb.exe`, as an attachment. After a crash, copy it before you start the game
  again: the next run renames it to `totk.1.log`.

Never attach game files, keys, builds or the shader cache, and never paste key values into a log or an issue.
