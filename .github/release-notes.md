DKB Recomp 0.5.2: a static recompilation of *Donkey Kong Bananza* (Switch 2) to a native Windows exe with a Vulkan renderer. It is still experimental (0.x). This release renames the project's settings and files from TotK to DKB, fixes the black shards of #439, makes 4 GB cards and shared-memory GPUs steadier, turns the draw merge on by default and adds Switch 2 controller support over USB.

**The packages hold no game data, and no release ever will.** You build the game on your own PC from your own dump of **1.0.0 or the 3.2.0 update**, given as a `.nup` or `.nx` file or as ExeFS and RomFS folders. Other updates are not supported.

## Read this first: the TOTK to DKB rename

The project is about Bananza, and it is built on TotK Recomp, whose credits stay in the README and in THIRD_PARTY_NOTICES.md. The names now say what the project is.

- **Settings are `DKB_*`.** `TOTK_*` environment variables are ignored. If any are set, the log has one line naming them (`cfg: N TOTK_ environment variable(s) ignored since the rename, use DKB_ instead`). Rename them in your launch scripts.
- **Files are `dkb.*`.** The executable, the tools, the installer, the tests and the settings example are named `dkb` instead of `totk`. The Windows window classes changed too.
- **Your settings carry over.** When there is no `dkb.ini` yet, an existing `totk.ini` is copied to it on the first run. Your saves are not touched.
- **Shaders are translated once more on the first start.** The shader translator changed, so its cache is rebuilt: the first start after updating takes as long as a first start did before, and later starts are fast again. If you update with the launcher, it rebuilds the changed steps for you.

## What is new since 0.5.1

- **Black shards in the Feast Layer (#439).** A 64-bit store of zeros to a register plus uniform address wrote its own guest address instead. The muck mesh compute pass starts its vertex normals this way, so some normals came out Inf/NaN, which was the black shards where muck meets slime. It depended on where the vertex buffer landed, which is why it showed on one PC and not on another. The zeros are stored properly now. Thanks @Dimiduh, @saxrussel and @supermahdi-enjoyer for the frame dumps and videos.
- **Missing grass on Linux (#462).** With shadow storage, the Linux default, the game's CPU reset of the plant list never reached the GPU, so the plants piled up near the camera and the open ground got none. The CPU's writes are now copied into the shadow before the part that writes the same range (#517, thanks @roverdrubber for finding it, and @jullson2 for the report).
- **4 GB cards (#437).** A nearly full 4 GB card left small pools imported, and the GPU read them across the bus. With `DKB_VRAM_LIMIT=4096` on the 1.0.0 tutorial cave, 208 small pools stayed imported and a frame's GPU time went from 7.7 to 16.3 ms (52.4 fps). Small mirrors now keep a smaller VRAM reserve: no pool stays imported, the GPU takes 8.2 ms a frame and the run holds 58.6 fps, the same as without a limit. Thanks @supermahdi-enjoyer for the logs.
- **GPUs that share system memory (#497).** An Iris Xe lost the device at 17.6 of 18.5 GB committed while its 8 GB heap showed half free, because the driver charges that memory to the commit limit. The VRAM budget is now capped at what the system can still commit (`DKB_MEM_COMMIT_RESERVE_MB`, default 2560), so textures and mirrors are released before an allocation fails. Thanks @Zagrack for the logs. The docs also explain the NVIDIA case, where the driver charges VRAM allocations to the process commit.
- **Replay threads on 6 logical processors.** Big render passes are recorded on 4 threads from 6 logical processors (it was 8 before). On a Ryzen 7 5700X held to 6 logical processors (RTX 5060 Ti, 1920x1080, 60 fps limit) this held 58.6 to 58.8 fps against 55.7 to 56.3 with 3 threads. With 4 or 5 logical processors it stays at 3 threads (no gain measured).
- **Draw merge is on by default (#468, thanks @wbkepler).** Runs of 8 or more draws that differ only in one uniform block are replayed as one instanced draw. The pipelines are remembered in `pipeline-keys.bin` and compiled at start. In the Hilltop Layer of Surf Hills (3.2.0, RTX 5090 and a 9800X3D) interleaved runs went from 98.7 to 111.0 fps. `DKB_DRAW_MERGE=0` turns it off.
- **Switch 2 controllers on USB (#496, #513, thanks @AngeloAscheri).** The release's `SDL3.dll` is built with libusb and ships `libusb-1.0.dll` beside it (LGPL-2.1, source in `licenses/`). The GameCube controller was listed but sent no input unless Steam Input woke it; it now starts without Steam. The Switch 2 stick calibration uses a radial deadzone and a circular clamp (#489, thanks @wbkepler).
- **`pad_background` (#502, #512, thanks @AngeloAscheri).** With `pad_background = 0` the gamepads count only while the game window is in front, as the keyboard does.
- **`pad_shortcuts` (#516, thanks @SercanDmr).** `pad_shortcuts = 0` turns off the controller shortcuts (Plus+Minus, Guide, L3+R3, the fullscreen hold), so every button goes to the game. On the Steam Deck they collided with Steam's own. It is live and has a toggle in F1 > Controls; F1 on the keyboard still opens the settings.
- **RenHDrum haptics, opt-in (#285).** If `renhdrum.dll` of a matching version is beside the exe and `renhdrum` is `auto`, player 1's raw vibration goes to it for detailed haptics, and the pads it claims are left alone by our own rumble. `rumble_hd` is listed in the example ini. `renhdrum = off` turns it off.
- **`lod_bias = auto`, opt-in (#488, thanks @roverdrubber).** With DLSS, FSR 3.1 or FSR 4 drawing below the output size, textures pick their mip level for the output size (the rule NVIDIA's and AMD's guides give). Off by default; it applies only while the temporal upscaler actually runs and is fixed at the first sampler, so a restart is needed after switching the upscaler.
- **Upscaler jitter for sliced passes (#503, thanks @roverdrubber).** The slices of a sliced pass get the upscaler's jitter as of the pass's start.
- **Crash at start on Linux (#510, #511, thanks @simonwjackson).** The gamepad subsystem is kept after the PlayStation pad probe, because its quit freed SDL's udev monitor and a device added at that moment crashed the game.
- **Spin attack at high frame rates (#480, thanks @wbkepler).** Stick-spin ring updates are paced to 60 Hz ticks above 60 fps, and the detection of the spin attack on a gamepad is fixed.
- **Launcher, installer and Windows (thanks @oshoubber, #456 to #467).**
  - Co-op input and rumble stay on the right controller during handoffs (#467).
  - Reinstalling works in a folder where uninstall kept saves and settings (#466).
  - An update works after the source archive was removed (#459), and the installed RomFS index is checked before it is reused (#463).
  - DLSS is offered only in builds that include the NGX backend (#464).
  - The window's physical size uses SDL's pixel density (#461).
  - The performance overlay shows when Bananza controls the frame timing (#460).
  - Redirected streams survive hiding a private Windows console (#458).
  - The Steam Deck profile explains why performance presets are unavailable (#457).
  - RecompLauncher starts with the bundled Linux build and exposes the game settings (#456).
  - The setup form says that ExeFS and RomFS folders are not needed with a `.nup` and that the release already holds the tool package (#433).
- **Install paths on POSIX (#451, thanks @wbkepler).** The prebuilt translator accepts install paths with special characters, and the Ninja commands of `build_game.py` are quoted.

## Also

- Speed, from @wbkepler's and @roverdrubber's pull requests: parallel slice collection costs less (#470), render targets released by the game are taken back without waiting for held slices (#469), per-draw vertex buffer binding, dynamic state and pipeline cache locking are pruned (#478), uniform snapshots are built per recording across calls (#472), the staged mirror skips redundant end barriers and zero-page copies (#455), a large pool is copied into its shadow on up to four threads (#482), new GPU writes merge into the shadow's blocked ranges without a full sort (#486), and a lazy write-back range that is still sealed skips its page walk (#481, #495).
- `mirror_budget`, off by default, sets a ceiling on the VRAM all pool mirrors take together (#487).
- Linux FSR 4 uses linear compute derivatives (#474).
- Lost asynchronous-preemption redirects are refused or expired, and pending ones are redirected from the fault handler, on by default (#449, #491, #492, thanks @wbkepler).
- Fixes of the rename's leftovers: the setting prefix length, duplicated names in tools and tests, and the stale hash of the BC3 encoder source.
- The AppImage is no longer also published as a `.tar.gz`.
- The MSYS2 libusb source package ships in `licenses/`.

## Pull requests in 0.5.2

Merged since 0.5.1, by author:

- @wbkepler: #451, #449, #469, #470, #472, #478, #480, #489, #491, #492, #468
- @roverdrubber: #474, #481, #482, #486, #487, #488, #495, #503, #517
- @oshoubber: #456, #457, #458, #459, #460, #461, #463, #464, #466, #467
- @AngeloAscheri: #512, #513
- @simonwjackson: #511

## Issues fixed or answered

#439 (black shards, @Dimiduh), #462 (missing grass on Linux, @jullson2), #437 (4 GB cards, @supermahdi-enjoyer), #497 (shared-memory GPUs, @Zagrack), #496 (Switch 2 controllers on USB, @ianrennie), #502 and #516 (gamepads in the background, shortcuts on the Steam Deck, @SercanDmr), #510 (Linux crash at start, @simonwjackson), #285 (HD haptics, @vink2212), #431 (mip bias, @roverdrubber), #433 (setup form, @sharicov). Some of these stay open until the reporter retests.

## Thanks

Pull requests in 0.5.2: wbkepler, roverdrubber, oshoubber, AngeloAscheri, simonwjackson. Issues, logs, saves and recordings: everyone named above. Thank you.

## Download

| Package | For | Status |
|---|---|---|
| `DKB-Launcher.exe` | Windows 10/11 x64 | The simplest way. Put it in a folder of its own and start it. It downloads the tools zip below, checks its SHA-256, builds the game from your dump and offers each later release with one click (docs/installer.md, "Launcher and updates"). |
| `DKB-Setup-<version>-windows.zip` | Windows 10/11 x64 | Supported. Carries its own tools and the installer window: unpack, run `DKB-Setup.exe`, pick your dump. |
| `dkb-release.txt` | the launcher | The version, file names, sizes and SHA-256 the launcher checks. |
| `DKB-Setup-<version>-linux-x86_64.tar.gz` | Linux x86-64, kernel 6.7 or later | Preview. Source package with `setup.sh`. You install the system packages it lists (`./setup.sh --check`), then it runs the command-line installer. |
| `DKB-Setup-<version>-x86_64.AppImage` | Linux x86-64 (SteamOS, any distribution with glibc 2.31 or later) | Preview. Self-contained: bundles Python, LLVM and a sysroot, so no system packages are needed. Make it executable and run it. |
| `DKB-Setup-<version>-macos-arm64.zip` | macOS 14.4 or later, Apple silicon | Preview. Same idea with `setup.command`. Play is not verified, and the destructible terrain is missing because Metal has no geometry stage. |

## Requirements (Windows)

- NVIDIA GPU with a recent driver (tested on an RTX 5060 Ti and an RTX 3070 Ti Laptop). AMD and Intel are experimental. On AMD's Windows driver the device loss most players hit is fixed (recorder thread off), but RADV on Linux can still hang (#99, #122, #198, #204). Intel Arc: #3.
- 16 GB of RAM plus a large page file (the process commits about 9 GB); 32 GB is comfortable. Cards with 4 GB of video memory should use `preset = 1` or `2` (docs/requirements.md).
- A controller is recommended. Switch 2 controllers on USB work without Steam.

## Known issues

- GPU hangs under RADV on Linux (#99, #122, #198, #204), and a driver fault on Intel Arc (#3).
- Black shards where salt or acid meets slime (#139, #266, #387). The muck case (#439) is fixed in this release; retest and tell us if you still see it.
- Crashes reported without a reproduction here: #126 (Lagoon to Hilltop tunnel, retest asked) and #148 (Linux, after the first boss).
- Hitches in the Resort and Tempest layers on 4 GB cards may remain in some scenes (#437; try `astc_bc = 1`).
- Another device loss on an RTX 5080 at B1000 (#336).
- On CPUs with 4 to 6 threads, heavy scenes stay around 40 to 45 fps. A first run with an empty pipeline cache still stalls about 3 s on its first heavy frame.
- Checkerboard textures (#5), #112 and unrecorded NVN 2 calls (#6).

## Reporting a bug

Open an issue with the log (`dkb.log`), the build line at its top (`dkb 0.5.2+<commit>`), your GPU and driver, and what you did before it failed. **Never share generated files, keys or screenshots that show game content.** Questions and chat: the [Discord server](https://discord.gg/kCkDsqu37y) (#help, #bugs, and #brasil in Portuguese).

## Support the project

Tips go to [ko-fi.com/lucasblippng](https://ko-fi.com/lucasblippng). A tip pays for time on the project; it buys no game files, builds or early access, and everything stays open source.

Roadmap: #10.


**Full Changelog**: https://github.com/Lucas-blip-png/dkb-pc-recomp/compare/v0.5.1...v0.5.2
