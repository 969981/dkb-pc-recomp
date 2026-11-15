<!-- Draft for the next release (0.4.0): kept up to date as changes land on bananza. Every pull request and closed issue goes in "Thanks". -->
DKB Recomp 0.4.0: a static recompilation of *Donkey Kong Bananza* (Switch 2) to a native Windows exe with a Vulkan renderer. It is still experimental (0.x). On recent NVIDIA cards players get well past the first level (Glacier, Snow World, Tempest), and it can crash.

**The packages hold no game data, and no release ever will.** You build the game on your own PC from your own dump of **1.0.0 or the 3.2.0 update**, given as a `.nup` file or as ExeFS and RomFS folders. Other updates are not supported.

## What is new since 0.3.0

- **Crashes fixed or guarded.** The Snow World descent device loss on NVIDIA is gone: one light quad (vertex stage ba1dd1e7ae8abb33) is skipped (#196, #197, #264). A crash while loading a save or the title screen at main+0x82fee0 is guarded (#157, #206, #173). The 3.2.0 game now has the layer-change guards 1.0.0 had (#218, as #126). On AMD's Windows driver the Vulkan recorder thread is off by default, which stopped the device loss for several players (#84, #124, #142, #155, #167, #176).
- **Picture.** The #9 light quads are drawn again, so their particles are back (#62), and the Tower light quads are drawn with a fold of their selector instead of faulting (#220). `scene_res = full` draws the 3D scene at the full output size instead of the 2/3 the game settles at (#210), on 1.0.0 and 3.2.0 (#223). Draws get depth twins like compute passes, so the validation layer is clean in the first level.
- **Performance.** Big render passes are recorded on 4 threads by default from 8 logical processors (not on AMD), dynamic uniform buffers are on by default (not on Intel), and the guest heap is lazy by default on Windows (commit about 9.3 GiB instead of 14.5 GiB). In the heavy -101 scene on an RTX 5060 Ti: about 57.5 fps with a 1% low of about 29 fps (0.3.0: 51.8 and 20.6). The end of a sliced pass no longer waits for its slices (#265), and small VRAM mirror write-backs are copied by one compute dispatch on NVIDIA (#269), and the destructible terrain's geometry-stage storage stays in VRAM instead of being copied back every frame (#268; Hilltop Layer on an RTX 5090: GPU time 16.1 to 12.4 ms a frame), with its bookkeeping kept off the mirror lock (#288). Command recordings reuse cached blocks and hand their buffer over instead of being copied, which removes almost all page faults in the Hilltop Layer (about 470,000 a second before, #289). Many lock and copy cuts in the sliced replay come from contributors (see Thanks).
- **DLC and mods.** Setup installs add-on content from a `.nup` into `aoc-<n>` folders (#183, #111, #258, #263). Loose RomFS mods load from a `mods` folder beside `dkb.exe` (#252).
- **Sound.** A Windows or macOS device that opens with a period that is not a multiple of the renderer's block is reopened with one block per period, which stopped the crackling on a 1024-frame device (#272).
- **High resolutions.** The game's GSys heap grows with the render-target pool, so `scene_res = full` at 3840x2160 and very large outputs no longer crash at start (#244, #279). A guide for 4 GB VRAM cards and laptops: `docs/4gb-vram-optimization.md` (#260).
- **Settings and input.** PlayStation button prompts when a DualShock or DualSense is connected at start (`pad_prompts`, #274). The vertical mouse camera no longer locks during automatic camera moves (#245). Big passes use 6 replay threads from 12 logical processors (#271). NVIDIA DLSS is listed in the upscaler row (#241), and its warm-up waits when another upscaler is chosen at start (#280). DLSS uses preset K except in Ultra Performance, which is cheaper than M on the GPU (`dlss_preset`, #287). `console = 0` hides the console window for frontends such as Playnite (#238). Controller connection handling, DK moving on walls and ceilings above 60 fps (#201), the 3.2.0 version on the title screen.
- **Installer.** It refuses a RomFS folder that holds a RomFS image (#278), compiles only the sources a release ships (#203, #208), recompiles every runtime source when any of them changes (#275), and builds the ultrawide overlays. RecompLauncher can set up and start the Linux and macOS packages, and a self-contained Linux AppImage can be built (#242, #174).
- **Diagnostics.** A crash outside the executable (a driver, a Vulkan layer) is reported with its module, and every crash writes a minidump to `logs/` after the report (#219); keep it on your PC and send it only when asked, privately.
- **Project.** Issue forms ask for the build, game version and OS; a security policy and an accessibility page were added.

## Thanks

Pull requests: wrothgaril (#223, #225, #255, #257, #259, #262, #265, #269, #268, #271, #273, #280, #287, #288, #289), GlebShekovtsov (#209, #211, #234, #274, #275), oshoubber (#226, #227, #228, #245), PorlocK-BR (#244), devadobe11-pixel (#260), Osohow (#242), thatbananaman08 (#217, #219, #220, #222), shadowlink1174 (#254, #277), Zorkats (#246, #250), Marjaperse (#261), MatyMeatBoy (#256), Felitendo (#213), lunar-me (#235, #236, #237). Thank you.

Testing and reports: everyone who sent logs, saves and input recordings, in particular luis-7654, philneitz, Felitendo and Dimiduh.

## Download

| Package | For | Status |
|---|---|---|
| `DKB-Setup-<version>-windows.zip` | Windows 10/11 x64 | Supported. Carries its own tools and the installer window: unpack, run `DKB-Setup.exe`, pick your dump. |
| `DKB-Setup-<version>-linux-x86_64.tar.gz` | Linux x86-64, kernel 6.7 or later | Preview. Source package with `setup.sh`: you install the system packages it lists (`./setup.sh --check`), then it runs the command-line installer. |
| `DKB-Setup-<version>-macos-arm64.zip` | macOS 14.4 or later, Apple silicon | Preview. Same idea with `setup.command`. Play is not verified and the destructible terrain is missing (Metal has no geometry stage). |

## Requirements (Windows)

- NVIDIA GPU with a recent driver (tested on an RTX 5060 Ti and an RTX 3070 Ti Laptop). AMD and Intel are experimental: device loss is still reported on some AMD cards (#3, #99, #193, #198, #204, #267).
- 16 GB of RAM plus a large page file (the process commits about 9 GB), 32 GB is comfortable.
- A controller is recommended.

## Known issues

- Device lost on some AMD cards (#3, #193, #267), and GPU hangs under RADV on Linux (#99, #122, #198, #204).
- A crash after a layer change on 3.2.0 at main+0x11a7e0 (#218), and crashes reported without a reproduction here (#126, #136, #148, #160, #229).
- Black shards where salt or acid meets slime (#139, #266).
- Text is soft at 1440p and above: the game's fonts are drawn at a fixed size (#214).
- Heavy scenes can still fall under 60 fps, and the first visit to an area can stutter.
- Checkerboard textures (#5), #112 and unrecorded NVN 2 calls (#6).

## Reporting a bug

Open an issue with the log (`dkb.log`), the build line at its top (`dkb 0.4.0+<commit>`), your GPU and driver, and what you did before it failed. **Never share generated files, keys or screenshots that show game content.**

Roadmap: #10.
