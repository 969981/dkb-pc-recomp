# Devlog

Milestones of DKB Recomp, newest first. The plan they follow is [bananza-plan.md](bananza-plan.md); open work is in
the [roadmap issue](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10).

## 2 November 2026

- **The repository opens for contributors**: roadmap issue, one issue per open task, a tester guide
  ([dkb-testing.md](dkb-testing.md)) and issue forms for Bananza.
- **Push constants located.** NVN 2 writes them as Ampere root tables, and the only constant bank above 17 the game's
  shaders read is bank 24, in the full-screen blit shaders. The lead is in
  [#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2).
- **GPU fault after the first guide movie: cause found** (eaac181). The game's neural upscaler reads shared memory out
  of bounds with data-dependent indices; the console tolerates it, a PC GPU faults about 6 runs in 10. Translator patch
  0013 clamps every shared and local memory index. Validation runs are in
  [#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9).

## 1 November 2026 (night)

- **Tutorial guide movies play** (e9a1feb). Punching in the tutorial opens a VP9 movie through the Switch 2
  `nn::codec` API; the host now decodes it with libvpx, following the contract of the game's own player.
- **Gameplay input**: Donkey Kong answers the controller in the tutorial cave.
- **The 3D scene renders** (5c1a10d): the tutorial cave, gold, grass and light shafts, 2,600 frames with no Vulkan
  validation error. Two causes: NVN 2 packs the compute driver bank per program, and the game uses textures as storage
  images without asking for it.
- **Every shader of the first 3D scene translates**: 4,820 of 4,820 Ampere stages lower to Maxwell and become SPIR-V.

## 1 November 2026 (afternoon)

- **First picture** (3a3e5a0): the first-boot mode menu, 40 s after start. The executable becomes `dkb.exe`.
- **Vulkan runs without a device loss** (c935940).
- **First Ampere shaders reach SPIR-V** (f080197): Eglantine's Ampere-to-Maxwell lowering runs when the game sets a
  program, and TotK Recomp's translator does the rest. DKB Recomp becomes GPL-3.0-or-later for it (9b13ff1).
- **Repository published.**

## 1 November 2026 (morning)

- **The main loop runs on a null renderer** (39d099c, f0e0fa3): 20 fps, audio playing, controller input and rumble.
- **Boot into NVN 2** (c34fe11, 2d7588d): through `nn::init`, 2,345 constructors, `nnMain`, the RomFS (75,238 files)
  and the save, up to the NVN 2 bootstrap (driver ABI 231.16).
- **The whole game lifts** (3941578): `main` and `sdk`, 180,061 functions, 585 MB of C with no unsupported
  instruction, after adding ARMv8.3 pointer authentication and `LDAPR`. The executable compiles and links.
