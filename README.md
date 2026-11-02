# DKB Recomp

**DKB Recomp** is a static recompilation of *Donkey Kong Bananza* (Nintendo Switch 2, 2025) into a native Windows
x86-64 application with a Vulkan renderer. It is built on [TotK Recomp](https://github.com/valentderah/totk-pc-recomp),
which does the same for *Tears of the Kingdom* on the original Switch.

> [!IMPORTANT]
> **No game files are included, and none ever will be.** You need your own copy of *Donkey Kong Bananza*, dumped
> from your own console. Instructions for obtaining those files will not be provided.

## Status

**Not playable yet.** The game boots, goes through the title screen into the tutorial cave, renders the 3D scene and
answers the controller: punching opens the first guide movie. It still crashes often, and many effects are missing.
There is no release and no installer.

| Part | State |
| --- | --- |
| CPU (ARM64 → C) | Done for base 1.0.0. `main` and `sdk` lift with no unsupported instruction (ARMv8.3 pointer authentication and `LDAPR` added). Differential tests against Unicorn pass. |
| OS (Horizon on Switch 2) | Boots, mounts the RomFS, creates the save, plays audio and reads the controller, all on TotK Recomp's host with the Switch 2 differences added. |
| Graphics (NVN 2) | The title screen and the tutorial's 3D scene render on Vulkan. 51 NVN 2 command buffer calls (mesh shaders, VRS, DLSS, push constants…) are accepted but not recorded yet. |
| Shaders (Ampere) | Every stage of the first 3D scene (4,820) goes through Eglantine's Ampere-to-Maxwell lowering and TotK Recomp's translator to SPIR-V. |
| Movies | The tutorial guide movies (VP9) decode on the host with libvpx. |
| Known crash | A GPU fault about 2 s after the first guide movie (an out-of-bounds shared memory access in the neural upscaler). Fixed in the translator, still being validated. |
| Not done | Push constants, colour grading LUT, blocky textures on some surfaces, saving and loading, performance, installer, update 458752. |

The full plan, phase by phase: [docs/bananza-plan.md](docs/bananza-plan.md). Shader notes:
[docs/bananza-shaders.md](docs/bananza-shaders.md). The design TotK Recomp and this port share:
[docs/architecture.md](docs/architecture.md).

## How to help

The project is at the point where each problem is a separate, well-bounded job. Pick one, say in an issue that you are
on it, and keep each pull request to one topic. These are open now:

- **NVN 2 push constants.** `nvnCommandBufferPush{Graphics,Compute}Constants` are written by the driver as GPU root
  tables (methods 0x504 and 0x508). Find which constant bank the shaders read them from, then record them in the
  Vulkan replay. Start at `runtime/nvn_api_nvn2.c`.
- **Testing on other GPUs.** Everything so far was checked on one NVIDIA card. Runs on AMD and Intel, with logs, show
  what breaks there. [docs/tester-guide.md](docs/tester-guide.md) has the switches.
- **Rendering bugs.** The colour grading 3D LUT comes out empty, and some rock surfaces look like a checkerboard (a
  texture format or deswizzle case). [docs/maxwell.md](docs/maxwell.md) lists the GPU behaviour the Vulkan path must
  match.
- **NVN 2 calls not recorded yet.** Each is logged on its first use (`NVN2_NOT_RECORDED` in
  `runtime/nvn_api_nvn2.c`). If a scene needs one, that scene tells you which.
- **Getting further into the game.** Play past the tutorial, find the next crash or hang, and report or fix it.
- **Installer.** Bring TotK Recomp's installer ([docs/installer.md](docs/installer.md)) to Bananza: extract the user's
  dump, generate and build on their machine.
- **Symbols.** Function names help debugging. They come from [GRAnimated/DKBananza](https://github.com/GRAnimated/DKBananza),
  which welcomes contributions too.

Before you open a pull request, read [CONTRIBUTING.md](CONTRIBUTING.md). Its hard rule: nothing that came out of the
game goes into the repository, not in commits, issues or pull requests. Run `python tools/check_content.py --staged`
before committing; CI runs the same check.

Bug reports use the issue forms. Include your GPU and driver, the log and the steps to reproduce.

## Building from source

The toolchain is TotK Recomp's ([docs/building.md](docs/building.md)): MSYS2 UCRT64 Clang, Ninja, Python 3.12 and the
.NET SDK for the shader translator. With the Bananza ExeFS and RomFS from your own dump:

```
.venv\Scripts\python tools\build_xlate.py
.venv\Scripts\python recomp\gen.py --exefs <exefs> --out generated\dkb100 --modules main,sdk
.venv\Scripts\python tools\dkb_expected.py --exefs <exefs> --name dkb100 --display 1.0.0
.venv\Scripts\python tools\dkb_pack_romfs.py --dir <romfs folder> --out build\extracted\dkb100\romfs
.venv\Scripts\python tools\build_game.py --version dkb100 --expected tools\data\expected-dkb100.json --exefs <exefs> --romfs build\extracted\dkb100\romfs
```

Everything the build writes stays under `build\` and `generated\`, which git ignores. The game uses about 13 GB of
RAM, so 16 GB is the minimum for now.

## Credits

- [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by valentderah, the base of everything here, and the
  projects it builds on: [LAS Recomp](https://github.com/peiikai/LASRecomp),
  [MacNativeGames](https://github.com/ly05010419/MacNativeGames), [nxnative](https://github.com/seedds/nxnative),
  [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp), [Ryujinx](https://github.com/Ryujinx/Ryujinx),
  [deko3d](https://github.com/devkitPro/deko3d), [hactool](https://github.com/SciresM/hactool) and the rest listed in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [GRAnimated/DKBananza](https://github.com/GRAnimated/DKBananza) for the game's symbols and research.
- [Eglantine](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork) (Eden Emulator Project) for the
  Ampere-to-Maxwell shader lowering and its Switch 2 notes, and [Mesa NAK](https://gitlab.freedesktop.org/mesa/mesa)
  for the SM70+ encodings both build on.
- [libvpx](https://chromium.googlesource.com/webm/libvpx) for the movies.
- [Switchbrew](https://switchbrew.org) for the Switch 2 `GetInfo` ids, and the Switch homebrew community.

## License

DKB Recomp as a whole is under the [GNU GPL v3 or later](LICENSE), because it includes Eglantine's GPL Ampere shader
lowering (`runtime/third_party/eglantine/`). The files that came from TotK Recomp keep their
[MIT License](LICENSE-MIT); third-party code keeps its own license ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
The license grants nothing for Nintendo's game.

DKB Recomp is an independent, non-commercial fan project, not affiliated with or endorsed by Nintendo. *Donkey Kong
Bananza* and *Nintendo Switch 2* are trademarks of Nintendo.

Most of the code and documentation is written by AI coding agents (Claude Code), as in TotK Recomp.
