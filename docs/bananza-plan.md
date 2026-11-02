# DKB Recomp plan

Goal: *Donkey Kong Bananza* (Switch 2, base 1.0.0) playable on Windows as a static recompilation, built on
[TotK Recomp](https://github.com/valentderah/totk-pc-recomp). No game files in the repository: each user dumps their
own cartridge and the build runs on their machine.

Known input: the base ExeFS has `main` (md5 f68752f5…, the binary GRAnimated/DKBananza's BinSync uses) and `sdk`, no
`subsdk0`. Update 458752 has another layout and is not supported yet. The RomFS is about 19 GB.

Each phase ends in a milestone someone else can check. The order follows the critical path.

## F0 — CPU: lifter and build (done)

- The TotK lifter runs on Bananza: 133,806 functions in `main`, 46,255 in `sdk`.
- ARMv8.3 instructions the Switch 2 code uses (commit 3941578): `retab`/`retaa`/`braa`/`blraa` as branches without
  authentication, `ldapr*` as `ldar*`, PAC hints as NOP. Each new class has a differential test against Unicorn.
- **Milestone reached:** all of `main` and `sdk` lift with no unsupported instruction, compile and link.

## F1 — OS: Horizon on Switch 2 (done)

- NSO loader for Switch 2 modules (RELR, MOD0), TLS, memory layout, `main.npdm`.
- TotK Recomp's host (threads, IPC, file system and RomFS, time, account, applet) with the Switch 2 `GetInfo` ids,
  the 9 GiB memory pool, `IAudioDevice` 19-21, `IAudioRenderer` 12-14 and the Switch 2 `oe`/`vi`/`account` calls.
- **Milestone reached:** the game leaves `nnMain`, mounts the RomFS and reaches its first NVN call.

## F2 — NVN 2 with a null renderer (done)

- NVN 2 bootstrap and proc table read from the game's own `sdk` (the driver is linked into the game, as on Switch 1).
  529 of the 700 entry points share NVN 1's names; the 171 new ones are listed in
  [bananza-nvn-new-procs.txt](bananza-nvn-new-procs.txt).
- With `TOTK_GPU=null` the main loop runs at 20 fps, audio plays and controller input arrives.
- **Milestone reached:** the game loop runs without a picture.

## F3 — Shaders: Ampere to SPIR-V (done for the first scene)

- Eglantine's Ampere-to-Maxwell lowering (`runtime/third_party/eglantine/`, GPL) runs at `nvnProgramSetShaders`. Its
  output goes to TotK Recomp's Maxwell translator (Ryujinx's, `tools/xlate/`) and comes out as SPIR-V.
- Fixes on top of Eglantine are marked "DKB Recomp" in its files. Translator fixes are patches in
  `tools/xlate/patches/`. Notes: [bananza-shaders.md](bananza-shaders.md).
- **Milestone reached:** every shader of the first 3D scene lowers and translates (4,820 of 4,820 stages).

## F4 — Vulkan back end (done for the first scene)

- NVN 2 compute constant bank c0 packed per program from the control section counts. Shared memory size read from
  the control. Bank 17 holds the Switch 2 blit parameters.
- Every single-sample colour texture can be used as a storage image, because NVN 2 does not ask for it.
- **Milestone reached:** the title screen and the tutorial cave render (2,600 frames, 0 validation errors).

## F5 — Playable (in progress)

- [x] Controller input in the menu and in gameplay (walking, punching).
- [x] Tutorial guide movies: `nn::codec` VP9 decoding on the host with libvpx (`runtime/ov_codec.c`).
- [~] GPU fault about 2 s after the first guide movie, in about 6 of 10 runs. Cause: out-of-bounds shared memory
      access in the neural upscaler. Translator patch 0013 clamps shared and local memory indices. One run with
      the patch passed the movie. A longer series is still to run.
- [ ] `nvnCommandBufferPush{Graphics,Compute}Constants`: the driver writes them as root tables
      (`SET_ROOT_TABLE_SELECTOR` 0x504, then `LOAD_ROOT_TABLE` 0x508). It is not yet known which constant bank the
      shaders read them from, so the host drops them. Commit 75d5e7b logs the first calls.
- [ ] Colour grading: the 3D LUT comes out empty.
- [ ] Some textures look blocky (checkerboard rocks): probably a format or deswizzle case.
- [ ] Saves: the save is created; saving and loading are not tested yet.
- [ ] Performance: so far the game presents every third vsync (20 fps). Nobody has profiled it yet.
- [ ] Installer on TotK Recomp's model (`docs/installer.md`): extraction from the user's dump, generation and build
      on their machine.
- **Milestone:** play from the start to the end of the first level without a crash.

## Later

- NVN 2 calls the host accepts and does not record yet (`runtime/nvn_api_nvn2.c`, `NVN2_NOT_RECORDED`): mesh shaders,
  variable-rate shading, DLSS, ray tracing, device command buffers. Each one is logged on its first call. None of
  them has mattered so far.
- Game update 458752.
- Linux, which TotK Recomp's toolchain supports.

## Alongside

- Symbols in [GRAnimated/DKBananza](https://github.com/GRAnimated/DKBananza) (nerve names, BinSync), which help debug
  every phase.
- Following Eden, oboromi, NeXo2 and any other NVN 2 work, and crediting what is used.

## Rules

- Nothing from the game in git: no ExeFS, RomFS, generated C, extracted shaders or screenshots with game assets.
- Licences: see [AGENTS.md](../AGENTS.md), "Reusing code".
- Small pull requests, one topic each, with honest numbers.
