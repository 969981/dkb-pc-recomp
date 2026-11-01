# DKB Recomp

**DKB Recomp** is an attempt at a static recompilation of *Donkey Kong Bananza* (Nintendo Switch 2, 2025) into a
native Windows x86-64 application. It is built on [TotK Recomp](https://github.com/valentderah/totk-pc-recomp), which
does the same for *Tears of the Kingdom* on the original Switch.

> [!IMPORTANT]
> **No game files are included, and none ever will be.** You need your own copy of *Donkey Kong Bananza*, dumped
> from your own console. Instructions for obtaining those files will not be provided.

## Status

**Not playable.** Very early: the CPU side works and the game starts booting; everything else is still to do.

| Part | State |
| --- | --- |
| ARM64 → C lifter | Done for base 1.0.0. `main` and `sdk` lift with no unsupported instruction (ARMv8.3 pointer authentication and `LDAPR` added). Differential tests against Unicorn pass. |
| Build | The generated C compiles and links into one executable with the TotK Recomp toolchain. |
| OS (Horizon on Switch 2) | Boots through `nn::init` and `nn::os` into the SDK module init on TotK Recomp's host; Switch 2 `GetInfo` ids added. Next: the file system and the services after it. |
| Graphics (NVN 2) | Not started. The game asks for 700 NVN entry points; 529 have the same names as NVN 1, which TotK Recomp implements. 171 are new (DLSS, new command buffer and program calls). |
| Shaders (Ampere) | In progress. Eglantine's Ampere-to-Maxwell lowering (GPL) takes 97.3% of 2,656 of the game's shader stages offline (`tools/dkb_lower_test.cpp`); next is feeding its output to TotK Recomp's Maxwell translator. |

The plan, by phase: [docs/bananza-plan.md](docs/bananza-plan.md). Shader notes: [docs/bananza-shaders.md](docs/bananza-shaders.md).

## Building from source

The toolchain is TotK Recomp's ([docs/building.md](docs/building.md)). With the Bananza ExeFS and RomFS:

```
.venv\Scripts\python recomp\gen.py --exefs <exefs> --out generated\dkb100 --modules main,sdk
.venv\Scripts\python tools\dkb_expected.py --exefs <exefs> --name dkb100 --display 1.0.0
.venv\Scripts\python tools\dkb_pack_romfs.py --dir <romfs folder> --out build\extracted\dkb100\romfs
.venv\Scripts\python tools\build_game.py --version dkb100 --expected tools\data\expected-dkb100.json --exefs <exefs>
```

Everything the build writes stays under `build\` and `generated\`, which git ignores.

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
- [Switchbrew](https://switchbrew.org) for the Switch 2 `GetInfo` ids, and the Switch homebrew community.

## License

DKB Recomp as a whole is under the [GNU GPL v3 or later](LICENSE), because it includes Eglantine's GPL Ampere shader
lowering (`runtime/third_party/eglantine/`). The files that came from TotK Recomp keep their
[MIT License](LICENSE-MIT); third-party code keeps its own license ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
The license grants nothing for Nintendo's game.

DKB Recomp is an independent, non-commercial fan project, not affiliated with or endorsed by Nintendo. *Donkey Kong
Bananza* and *Nintendo Switch 2* are trademarks of Nintendo.

Most of the code and documentation is written by AI coding agents (Claude Code), as in TotK Recomp.
