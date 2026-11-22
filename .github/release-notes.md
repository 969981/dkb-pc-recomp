DKB Recomp 0.5.1: a static recompilation of *Donkey Kong Bananza* (Switch 2) to a native Windows exe with a Vulkan renderer. It is still experimental (0.x). A fix release for 0.5.0: on Windows with an NVIDIA GPU, 0.5.0 compiled its compute pipelines again at every start, which held the game near 30 fps for the first minutes.

**The packages hold no game data, and no release ever will.** You build the game on your own PC from your own dump of **1.0.0 or the 3.2.0 update**, given as a `.nup` or `.nx` file or as ExeFS and RomFS folders. Other updates are not supported.

## What is new since 0.5.0

- **NVIDIA shader cache on Windows.** 0.5.0 pointed NVIDIA's shader disk cache at an `nv-cache` folder beside `dkb.exe` but never created that folder. The Windows driver reads that setting and kept no disk cache, so every start compiled the compute pipelines again in the background (about 5 minutes on a GTX 1650, with the game near 30 fps meanwhile; 0.4.2 took 0.2 s). 0.5.1 creates the folder. The first start fills it, and later starts are fast again (here 12.5 s to 0.13 s). Thanks @supermahdi-enjoyer for the four logs that showed it (#437).
- **Player 2's rumble.** With two gamepads the game reads both, also in single-player, and sends the rumble for the pad you play on to that pad's own slot. Player 2's slot was ignored, so the rumble came out of the other pad. Player 2's rumble now plays on its own gamepad (two motors), and the log names both pads' ids (#445, thanks @mrflanderson for the two-pad test).

## Also in 0.5.0 (since 0.4.2)

- **Windows launcher.** `DKB-Launcher.exe` is one small file. It downloads the build tools from the latest release and checks their size and SHA-256, then builds the game from your dump and shows Play. At each start it looks for a new release (a tick box turns that off), shows the notes and updates in place with one click. An update downloads only the new tools zip, rebuilds only the steps whose inputs changed, and keeps your saves (`profile/`) and `dkb.ini` (#420, #140, #402). The setup zip still works as before.
- **Performance presets.** `preset = 1..5` (Minimum to Ultra) or `recommended`, which picks a level from the GPU's video memory, the RAM and the processor count at start. A preset only fills in defaults, so any option you set yourself still wins, and the F1 menu has a Preset row. docs/requirements.md lists the resolution and frame rate to expect at each level.
- **Upscaler quality.** `dlss_mode` picks the DLSS, FSR 3.1 and FSR 4 mode by name (DLAA, Quality, Balanced, Performance, Ultra Performance). The setup can fetch NVIDIA's DLSS DLL and AMD's FSR 4 from their repositories after you accept their licenses; both are off by default (#423).
- **Local co-op.** A second gamepad plays Pauline (Pause > Co-op mode > Share controllers, #240). `coop = 0` turns it off.
- **Languages.** `lang = th` plays update 3.2.0 in Thai (#180). Guest `setjmp`/`longjmp` now work, which the Thai text layout needs.
- **16:10 screens** (1280x800, 1920x1200, 2560x1600). The view grows taller and the HUD stays whole in a centred 16:9 area. The title menu and loading tips fill the screen instead of leaving purple bands (#249). Tested on 3.2.0 and 1.0.0.
- **Button prompts.** PlayStation prompts follow your `bind_` lines, not only `pad_layout` (#416). The game's Switch-letter prompts follow X/Y and A/B swaps, and `pad_layout = xbox_prompts` now works in Bananza too (#440). Run the setup again to rebuild the prompts.
- **Start-up.** A start-up screen shows real progress in the game window until the game's first picture (#430; `splash = 0` turns it off).
- **Loading and memory.** The staged mirror is on by default on Windows with an NVIDIA GPU. The frame that enters a level or layer takes about half as long (worst frame 444 to 230 ms on an RTX 5060 Ti 8 GB; #442, #424, #429; `mirror_staged = 0` turns it off).
- **Fewer CPU threads.**
  - Recording a draw costs about 14% less. On 6 logical processors, frames over 50 ms in the heaviest scene tested fell from about 22 to 15 a minute (`DKB_DYN_KEEP=0` brings back the old path).
  - The replay runs above normal priority while it submits a frame, so loading threads no longer starve it. On 6 logical processors a level transition's replay fell from about 11 ms a frame to 0.25 ms.
- **AMD.** `replay_threads` is on by default on every discrete card on AMD's proprietary driver, not only the RX 9070 series (#141, #418). Integrated GPUs, AMDVLK and RADV keep it off, and the RX 5000 series is not measured yet.
- **Crashes.**
  - Picking up some bananas in a busy area crashed 1.0.0. The game's per-frame GPU buffer list walk now stops at the list's 256 entries (#428).
  - A pool guard at `main+0x429594` (3.2.0) and `main+0x410794` (1.0.0) checks the element it loads, not only the array (#443, #204).
  - With `realtime = 1`, the frame rate is held to 120 fps at most (`realtime_max_fps`; 0 removes the ceiling). Shorter steps crash the record merge job in the EmeraldRush challenge levels (#411, #421, #441).
- **Crash reports.** A second faulting thread waits for the first one's report instead of cutting its register dump short. On Linux and macOS, a fault in a driver or a Vulkan layer is reported by file name (#443).
- **Setup.**
  - "Use the RomFS where it is" (`--romfs-in-place`, #305) reads your extracted `romfs` folder through `romfs_dir` instead of making a ~9 GB copy. Keep the folder where it is afterwards.
  - The setup no longer asks for the dump folders when they were moved after an install. It verifies and uses the copy already in the install folder (#438).
  - When the setup's own `bin` folder looks incomplete, it names the folder and the files it misses (#433).
  - The shader cache, about 118,000 loose files on 3.2.0, is folded into one `shader-cache.pack`, so deleting or copying an install no longer takes tens of minutes.
  - The finish screen shows where the kept build files are and how big they are, and can delete them. An Uninstall button, or `--uninstall` (`--delete-saves` also removes saves), removes the game and its build files.
- **Launchers and frontends** (#402).
  - `dkb.exe --set KEY=VALUE` sets a `dkb.ini` key for one run, `dkb.exe --help` lists the options, and an unknown option is named.
  - `DKB-Setup.exe` and the AppImage pass a setup command line on to the command-line setup. `--play [DIR]` starts the finished game, and the setup window takes `--source` and `--dest` to prefill its form.
  - The AppImage keeps its work folder inside the install folder.
  - The NVIDIA shader cache on Linux, the flight recorder, and the uv, NuGet and dotnet caches stay in the install or work folder. A test fails if a setup writes anything to the home folder.
  - The Windows zip ships a RecompLauncher profile (`recomplauncher.json`).
- **macOS.** The install check finds the Vulkan loader and the headers on Apple silicon (#444). `DKB_HEADLESS=1` runs the game with no window, for captures and scripted runs. CI now compiles the whole runtime on macOS at every push.
- **Picture.** Text and the HUD are sharp at 1440p and above with `scene_res = full`. Without it, the HUD is drawn at two thirds of the screen size and stretched (#214).

## Pull requests in 0.5.0

No pull requests since 0.5.0. Thank you to everyone who sent code for 0.5.0; each one below is new since 0.4.2:

- #419 Keep the Bananza installer tests independent of the test machine's free disk space and symlink rights (@wrothgaril)
- #420 DKB-Launcher.exe: a small Windows launcher that installs the build tools, checks for releases and updates in place with one click (@wrothgaril)
- #422 Stamp release files with the commit time (@GlebShekovtsov)
- #423 Let the Bananza setup fetch AMD FSR 4 from AMD's repository once the user accepts AMD's license (@GlebShekovtsov)
- #424 Take the staged mirror's bookkeeping off the replay thread, and commit guest memory ahead of use in the background (@wrothgaril)
- #425 Name the disk that is short of space in the Bananza installer's check (@cybersecurityhandle)
- #429 Yield after each slice of the background commit thread, so a waiter for the memory lock gets in (@wrothgaril)
- #430 A start-up screen with real progress in the game window (@wrothgaril)
- #436 Two off-by-default scheduler settings against mutex convoys: `sched_handoff = 1` and `mutex_spin = <us>` (@wrothgaril)
- #442 The staged mirror on by default on Windows with an NVIDIA GPU (@wrothgaril)
- #444 macOS fixes: the install check, a headless test mode, and the new upscaler hashes (@anthonyyya)

## Issues fixed or answered

#437 (NVIDIA shader cache, 0.5.1, @supermahdi-enjoyer) and #445 (two gamepads, rumble on the wrong pad, @mrflanderson). In 0.5.0:

#428 (banana pickup crash, @prayudigames93-ctrl, with dumps and a save), #443 (pool guard crash, @Felitendo, with the disassembly), #204 (@MedoShinwa), #411, #421 and #441 (EmeraldRush crash with `realtime`, @NoireOvO, @crni20zd, @Nikoroq), #240 (local co-op, @keerttttt), #180 (Thai, @RavenChz, who also tested the mod route), #249 (16:10, @dextert007), #305 (RomFS in place, @drboulakdemhadj-sys), #416 (DualSense prompts, @dancaiman), #440 (prompt swap, @Nikoroq), #438 (@kline1110) and #433 (@sharicov) (setup errors), #402 (launcher and packaging, @wrothgaril), #214 (text at 1440p, @Felitendo), #141 and #418 (AMD frame rate, @GlebShekovtsov, @AcousticBlu3, with runs from @akmuzyka and @Ghos796), #140 (launcher, @Koaxz). Some of these stay open until the reporter retests.

## Thanks

Pull requests in 0.5.0: wrothgaril, GlebShekovtsov, cybersecurityhandle, anthonyyya. Issues, logs, saves and recordings: everyone named above. Thank you.

## Download

| Package | For | Status |
|---|---|---|
| `DKB-Launcher.exe` | Windows 10/11 x64 | New, the simplest way. Put it in a folder of its own and start it. It downloads the tools zip below, checks its SHA-256, builds the game from your dump and offers each later release with one click (docs/installer.md, "Launcher and updates"). |
| `DKB-Setup-<version>-windows.zip` | Windows 10/11 x64 | Supported. Carries its own tools and the installer window: unpack, run `DKB-Setup.exe`, pick your dump. |
| `dkb-release.txt` | the launcher | The version, file names, sizes and SHA-256 the launcher checks. |
| `DKB-Setup-<version>-linux-x86_64.tar.gz` | Linux x86-64, kernel 6.7 or later | Preview. Source package with `setup.sh`. You install the system packages it lists (`./setup.sh --check`), then it runs the command-line installer. |
| `DKB-Setup-<version>-x86_64.AppImage` | Linux x86-64 (SteamOS, any distribution with glibc 2.31 or later) | Preview. Self-contained: bundles Python, LLVM and a sysroot, so no system packages are needed. Make it executable and run it. |
| `DKB-Setup-<version>-macos-arm64.zip` | macOS 14.4 or later, Apple silicon | Preview. Same idea with `setup.command`. Play is not verified, and the destructible terrain is missing because Metal has no geometry stage. |

## Requirements (Windows)

- NVIDIA GPU with a recent driver (tested on an RTX 5060 Ti and an RTX 3070 Ti Laptop). AMD and Intel are experimental. On AMD's Windows driver the device loss most players hit is fixed (recorder thread off), but RADV on Linux can still hang (#99, #122, #198, #204). Intel Arc: #3.
- 16 GB of RAM plus a large page file (the process commits about 9 GB); 32 GB is comfortable. Cards with 4 GB of video memory should use `preset = 1` or `2` (docs/requirements.md).
- A controller is recommended.

## Known issues

- GPU hangs under RADV on Linux (#99, #122, #198, #204), and a driver fault on Intel Arc (#3).
- Black shards where salt, acid or muck meets slime (#139, #266, #387, #439). On one PC the muck's vertex normals come out NaN from a GPU compute pass; it does not happen on our test PC.
- Crashes reported without a reproduction here: #126 (Lagoon to Hilltop tunnel, retest asked) and #148 (Linux, after the first boss).
- Hitches in the Resort and Tempest layers on 4 GB cards, from video memory pressure (#437; try `astc_bc = 1`).
- Another device loss on an RTX 5080 at B1000 (#336).
- On CPUs with 4 to 6 threads, heavy scenes stay around 40 to 45 fps. A first run with an empty pipeline cache still stalls about 3 s on its first heavy frame.
- Checkerboard textures (#5), #112 and unrecorded NVN 2 calls (#6).

## Reporting a bug

Open an issue with the log (`dkb.log`), the build line at its top (`dkb 0.5.1+<commit>`), your GPU and driver, and what you did before it failed. **Never share generated files, keys or screenshots that show game content.**

Roadmap: #10.
