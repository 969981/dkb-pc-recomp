# Devlog

Milestones of DKB Recomp, newest first. The plan they follow is [bananza-plan.md](bananza-plan.md); open work is in
the [roadmap issue](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10).

## 3 November 2026

- **ASTC textures written by buffer-to-image copies decode on the GPU** (#38, by Kvinkxerli). Such a copy into an ASTC
  texture used to be skipped; with the GPU decoder on, tight copies now go through it in the replay, which then
  rebinds its own compute state. Copies with padded rows are still skipped. Kvinkxerli's run got through the Ingot
  Isle Cave-In cutscene to the Baboonburg save point with it. In the tutorial here, the one ASTC copy still skipped is a
  different kind (texture to texture, clear or readback), and 2 runs of 600 s showed no regression.
- **The later GPU fault is skipped too** (6da41ba, [#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
  In 9 runs of 600 s, every run where one more light quad (NVN 2 vertex stage cae0ef8e9fe9147f) drew faulted, with its
  draw in flight at the device loss, and the runs where it never drew did not. Its fragment stage does not read
  c0+0x530, so the first skip missed it. It is now skipped on NVIDIA as well, and 6 runs of 600 s since had no fault,
  including one where it came up. Its nine per-instance streams match a quad that never faults except for the
  instance count, and so do its SPIR-V features, so it is matched by name and the cause is still open.
- **Small Switch 2 reads can skip the GPU wait, as an opt-in** (#24, by NotShinySource, with monsterlag's opt-in).
  Bananza invalidates 128 bytes of a pool every frame, and with the asynchronous replay that read waited about 15 ms
  for the frame's GPU work. With `nx2_read_nowait = 1` and `replay_async = 1`, a read of up to 4 KiB no longer waits
  and its write-back is queued after the submissions it would have waited for. It stays off by default because that
  write-back can overwrite CPU writes made after the invalidate returns. The same PR fixes the replay thread counting
  release items as finished presents, which could end the one-frame present bound early.
- **Partial stencil clears are replayed** (#36, by monsterlag). The mining tunnel clears stencil with write mask
  `0x1`, which the host used to skip. Vulkan's clears ignore the write mask, so a full-screen triangle now replaces
  only the masked bits, in its own pass per layer, leaving depth and the other stencil bits alone. A 600 s run here
  had no fault and 0 validation errors. Nobody has shown yet whether this changes the black cave surfaces.
- **Timestamp reports keep their submission's tick** (#37, by monsterlag). The tick a report carries used to be
  looked up in a 1,024-slot ring by fence value, so a report that completed before the submit published its slot, or
  after another fence reused it, fell back to the completion time and could run backwards (100 ms, then 20 ms). The
  tick now travels with the submission to each report.
- **The later GPU fault comes from one stage pair** ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
  Logging every distinct stage pair showed that the runs where one instanced quad's vertex shader drew all faulted,
  4 of 4, and the two where it never came up didn't. Leaving out only that vertex stage gave 0 faults in 4 runs of
  600 s. Nothing in its SPIR-V is rare; what it shares with the early light quads is still being narrowed down.
- **Guest thread exits check their jump frame** (#34, by monsterlag). After #33, both thread entry points stop with a
  clear error if the CRT's `setjmp` recorded no frame, instead of failing fast later in `longjmp`.
- **The later GPU fault is not about pipeline timing.** With pipelines built synchronously (`TOTK_PIPE_SYNC=1`,
  `TOTK_PIPE_WAIT=draw`, as `--shot` does), 2 of 4 runs of 600 s still faulted around 7 minutes in, the same rate as
  with background builds. The vertex shader on the GPU changes from fault to fault
  ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **Furthest point so far** (philneitz, by hand on an RTX 5090 Laptop): through the storm cutscene and the cave-in to
  the Ingot Isle Cave-In section, where dying and respawning stalled the GPU at about 0.1 FPS
  ([#10](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/10)).
- **Compute push constants land where the shaders read them** (#20, by monsterlag). NVN 2 packs a compute program's
  root tables into c0 after its 16-byte head, each padded to 16 bytes, before the buffer tables; the shader control
  gives each table's word count. A test run showed the game pushing 32 bytes into root table 0 of the upscaler: its two
  image rectangles (`{0, 0, 1919, 1079}` at 1080p, changing with the dynamic resolution), which the host used to guess
  from the bound images. The tutorial looks the same; the translator has to be rebuilt
  ([#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2)).
- **ExitThread works with Windows context IP validation** (#33, by zswxp59). The thread exit's `longjmp` now uses
  MinGW's SEH-aware `setjmp`; the non-SEH form failed with `FAST_FAIL_INVALID_SET_OF_CONTEXT` on Windows 26300
  ([#31](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/31)).
- **An installer for Bananza** (#19, by monsterlag). From your own base 1.0.0 ExeFS and RomFS folders it packs the
  RomFS, builds the shader translator, lifts the code with the patch table and builds `dkb.exe`, from this checkout
  with the toolchain listed in [installer.md](installer.md).
- **The GPU fault after the first guide movie is not one shader's.** Leaving out the vertex shader that faults moves
  the fault to another vertex shader of the same instanced light-quad family. Three NVIDIA drivers hit it at the same
  point: 621.01 and 617.42 with an invalid instruction pointer, 617.14 with a crash inside the driver's shader
  compiler ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **A mitigation for that fault** (84a8f7d). On NVIDIA, the light-quad draws whose vertex stage fetches ten
  per-instance streams are skipped (`TOTK_FIX_VS_FAULT=0` draws them). The rest of the family draws, so the scene
  keeps its lighting. In two 600 s runs, one played to the end (30 FPS in its last minutes) and one faulted later,
  deep in the mining tunnel; without it every run faults right after the first movie.
- **The tutorial's guide movies play** (f5f1a23). Their player sends packets until one comes back not ready, then
  waits on the decoder's submission-ready event, which `nn::codec` signalled only once. The player stopped at its
  second picture, the flat yellow card the movies open with, so the guide window showed a yellow box. Reserving an
  output buffer now signals the event, and the window shows the movie.
- **Contributors' fixes merged**: the Switch 2 counter at 31.25 MHz (#25), timestamp reports at submit time (#26),
  the README's missing patch table (#16), the replay drain (#23), compute overlap mode (#18), interface array
  reflection (#21) and IPC session lifetime (#17). The tutorial still runs at 20 FPS most of the time here; one run
  reached 30 FPS for its last minute. The game's own pacer accounts for most of each frame.

## 2 November 2026

- **Ten minutes into the tutorial with a workaround for the GPU fault.** Dropping the lighting draws whose fragment
  shader reads the storage buffer at c0+0x530 (a local diagnostic switch, not committed) lets a 600 s scripted run
  (walk, camera, punches) play the guide movie and go on into the mining-tunnel tutorial with no fault and no hang.
  The scene is dark without those draws, and the guide-movie window shows a flat yellow box. VRAM pressure is ruled
  out as the cause: at the fault the device-local heap is at 2666 MiB of 7123 MiB
  ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **Block-linear textures use the Switch 2 GOB layout** (f4f7b17). Inside a 64 x 8 GOB, Switch 2 swaps bits 5 and 6
  of the offset, as Eglantine's `NativeGobOffset` does for every nx2 image. With the Switch 1 layout, textures read
  straight from the game files came out scrambled; the gold rock in the tutorial cave now has its crystal facets
  ([#5](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/5)). Textures the copy engine writes round-trip
  through the same layout, so they looked right either way.
- **The freeze after the first guide movie is the same GPU fault.** A debugger on a frozen run found a game thread
  spinning in `vk_upload_flush` (`runtime/vk_memory.c`): the device was already lost, `vkWaitForFences` returned at
  once and the loop never checked it. The wait now reports the loss, and 4 of 4 runs end with the same device fault
  (invalid instruction pointer) instead of a freeze. The fault itself is still open
  ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **Eglantine is the default reference** for NVN 2, GPU and Switch 2 service bugs; `tools/fetch_refs.sh` fetches it.
  Compared at this fault: it serves the movie through the `gsv` service, and at its default GPU accuracy it does not
  copy GPU-written textures back on CPU reads, which we do.
- **NVN 2 push constants recorded** (33628c7). A run confirmed the lead: the game pushes `{1/w, 1/h, w, h}` into
  graphics root table 0, exactly what its full-screen blit shaders read from bank 24, and the replay now binds those
  values instead of a guessed block. No compute shader reads bank 24, so where compute reads its pushes is still open
  ([#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2)).
- **The GPU fault after the first guide movie is not solved.** Translator patch 0013 alone did not stop it; with push
  constants in place, one run of two faulted and the other froze, both right after the first movie frame. The fault
  type (invalid instruction pointer) now points at shader code that goes away while the GPU still runs it; runs with
  synchronous pipeline builds test that ([#9](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/9)).
- **The repository opens for contributors**: roadmap issue, one issue per open task, a tester guide
  ([dkb-testing.md](dkb-testing.md)) and issue forms for Bananza.
- **Push constants located.** NVN 2 writes them as Ampere root tables, and the only constant bank above 17 the game's
  shaders read is bank 24, in the full-screen blit shaders. The lead is in
  [#2](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/2).
- **GPU fault after the first guide movie: first lead** (eaac181). The game's neural upscaler reads shared memory
  with data-dependent indices, and padding shared memory to 48 KB seemed to stop the fault, but those runs froze
  instead, which the count missed (see above). Translator patch 0013 clamps every shared and local memory index; it
  turned out not to be enough.

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
