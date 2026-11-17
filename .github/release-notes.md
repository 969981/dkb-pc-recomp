## Draft: next release (since 0.4.0)

Draft, not released. It lists what is on `bananza` since the tag `v0.4.0`. The 0.4.0 notes follow below, unchanged.

Not run in play by us: the Lanczos window filter at 1440p, AMD FSR 3.1 frame interpolation, AMD Anti-Lag 2 on AMD hardware, FSR 4 on AMD hardware and HD rumble on real controllers. The CRT and window filters and the frame interpolation come from community pull requests and have had little testing in play.

### What is new

- **Crashes.** A null edge target in the job graph builder is guarded on 1.0.0 and 3.2.0 (#317). A call that crashed after the Forest Layer snow tunnel gets a guard that skips it when its slot is null (#136, still open until the reporter retests). A GPU that finishes nothing for 30 seconds is reported as a device loss instead of a silent freeze (#329). A failed swapchain rebuild destroys the retired swapchain (#332). The frame generation display thread no longer takes its start lock under the present lock (#343).
- **Picture and upscalers.** `upscaler = fsr3` is AMD FSR 3.1 temporal upscaling and `upscaler = fsr4` is AMD FSR 4 through your own AMD DLLs (#270, #333); both are off by default and fsr4 has not been run on AMD hardware by us. DLSS, FSR 3.1 and FSR 4 also run as native anti-aliasing at `scene_res = full` on 3.2.0 (#345). Only draws that test or write depth are jittered (#321, #325), and the upscaler dispatch is found by its shape when its hash is not a listed one (#323). The upscaler row stays open on Bananza, and DLSS says in the log why it does not run (#152, #306). Window scaling filter: `window_filter = fsr1`, `linear`, `catmull_rom` or `lanczos`, with Sharpness at any window size (#340); `fsr1` stays the default, and Lanczos at 1440p has not been run by us. CRT filters: `crt_filter = off` (default), `lottes_grille`, `lottes_tv`, `hyllian` or `zfast`, with `crt_lines` (#342). ASTC textures stored as BC3 are copied instead of skipped, which is meant to bring back the Ingot Isle textures (#310, awaiting the reporter). Text above 1080p is sharper behind `DKB_FONT_SCALE=1` (#214, off by default). Dug terrain builds on RADV (#322).
- **Frame generation.** `frame_gen` has an Auto mode and 3x to 8x besides 2x, with the pacing they need (#334); it is off by default. At 2x, `frame_gen_by = fsr3` makes the frame with AMD FSR 3.1 frame interpolation instead of the built-in one (#346, off by default, not run by us), and it gets the frames without their HUD (#348). Frames are also made from the game's own HDR picture (#337). The Display tab has a Frame generation row and a Made by row (#349), and a set that waited is counted in the log (#352).
- **Performance.** The uniform snapshot takes shortcuts (`TOTK_USNAP_FAST`) and deferred guest reads are written back without waiting, on by default, about 1.5 ms a frame in Canyon Layer (#312). `TOTK_MIRROR_OWN` leaves the VRAM mirror's own GPU writes out of the sync (#295), `TOTK_CALL_REF` records some CallCommands as one call (#307), and a submission's GPU writes are copied back in one pass (#328). `TOTK_DRAW_MERGE` draws runs of c4-only draws as instanced draws and is off by default (#300). The end of a sliced pass is held back without the recorder thread too (#292), and sliced passes are recorded by default on AMD's proprietary driver on the RX 9070 and 9070 XT, the chip measured (#298; RDNA 1 and 2 are left out, #84, #155). An upscaler that is no longer chosen releases its images after 120 frames (#351). On Linux the shader disk caches get 10 GiB of room unless you set a size (#330), and on Intel under Windows the pipeline cache data is fetched only while no pipeline is being built (#331).
- **Sound.** The audio renderer is paced evenly instead of in bursts after each device period, which is meant to remove the crackling on a 1024-frame device (#133, #272, #347); #272 awaits the reporter.
- **Languages and text.** Polish (`pl`, `EUpl`, `pl-PL`) is a language choice (#335).
- **Input.** `pad:touchpad` can be bound, so the DualSense or DualShock 4 touchpad click can open the map (#291). HD rumble goes to a Switch Pro Controller or Joy-Con as amplitude and frequency (#285, not run on real controllers by us). The Controls tab builds against SDL older than 3.4.18 (#324).
- **Installer and Linux.** The setup's host check builds against the release sysroot on Linux, so the AppImage no longer fails at step 1 (#293), and its last check accepts a CPU that has no AVX2 when you build for x86-64-v2 (#318). `vulkan_driver` runs a Mesa build of your own on Linux without a wrapper script (#339). The `--warm-shaders` and `--prepare-shaders` library builds run at below-normal priority on one core fewer than the machine has, from 4 cores up (#3).
- **Diagnostics.** The performance overlay has a Breakdown mode that stacks each frame's parts in the graph (#341) and a Counter mode, a large FPS number with a small label (#350); both are values of `perf_overlay`. AMD Anti-Lag 2 is `antilag = 1`, off by default, on an AMD GPU whose driver has VK_AMD_anti_lag (#338); not run on AMD hardware by us. `TOTK_MEM_REPORT=N` logs where the process's commit goes, and `tools/lowend_bench.py` runs a low-end benchmark.

### Pull requests in this release

Thank you to everyone who sent code. Each one below is on `bananza` since 0.4.0:

- #292 Hold back the end of a sliced pass without the recorder thread too (@GlebShekovtsov)
- #294 Read page stamps under gen_lock in TOTK_MEMORY=verify (@Zorkats)
- #295 Leave the VRAM mirror's own GPU writes out of mirror_sync (TOTK_MIRROR_OWN) (@Zorkats)
- #296 Commit the probe's heap before the freeze checks (exe-probe-gfx-abi with the lazy heap) (@thatbananaman08)
- #298 Record sliced passes by default on AMD's proprietary driver too (@GlebShekovtsov)
- #300 Draw runs of c4-only draws as instanced draws (TOTK_DRAW_MERGE, off by default) (@Zorkats)
- #307 Record CallCommands of a recording without deferred records as one call instead of a copy (TOTK_CALL_REF) (@thatbananaman08)
- #312 Take shortcuts in the uniform snapshot and write deferred reads back without waiting, about 1.5 ms a frame in Canyon Layer (@Zorkats)
- #321 Jitter only the draws that test or write depth, so the temporal upscalers are not handed a picture shifted twice (@roverdrubber)
- #322 Build the geometry stage of dug terrain on RADV, whose input locations past 15 the location check refused (@roverdrubber)
- #323 Take the game's upscaler dispatch for DLSS, FSR 3.1 and FSR 4 by its shape when its hash is not one of the listed ones (@roverdrubber, landed by hand)
- #324 Build the Controls tab against SDL older than 3.4.18, whose headers have no SDL_GAMEPAD_TYPE_STEAM (@roverdrubber)
- #325 Jitter the scene draws by their depth test alone, as the review of #321 noted, and say which draws that leaves out (@roverdrubber)
- #326 Say in geometry_validate that taking the vertex stage's location limit on RADV is deliberate, as the review of #322 asked (@roverdrubber)
- #328 Copy a submission's GPU writes back to guest memory in one pass (@roverdrubber, landed by hand)
- #329 Report a GPU that finishes nothing for 30 seconds as a device loss (@roverdrubber)
- #330 Give Mesa's and NVIDIA's shader disk caches 10 GiB of room on Linux unless the player set a size (@roverdrubber, landed by hand)
- #331 Fetch the pipeline cache's data only while no pipeline is being built on Intel under Windows (@roverdrubber, landed by hand)
- #332 Destroy the retired swapchain when creating its replacement fails (@roverdrubber, landed by hand)
- #333 Give FSR 4 an input pass, FSR 3.1's output pass, a Linux library and depth flags (@roverdrubber)
- #334 Add frame generation's Auto mode and the 3x to 8x multipliers, with the pacing they need (@roverdrubber, landed by hand)
- #337 Make frames from the game's own HDR picture too (@roverdrubber)
- #338 Add AMD Anti-Lag 2 as an option (antilag = 1) (@roverdrubber)
- #339 Add an optional vulkan_driver setting so a Mesa build of the player's own can run the game on Linux without a wrapper script (@roverdrubber)
- #340 Give the window a choice of scaling filter, with Sharpness at any window size and the game's own HDR picture for the filters chosen (@roverdrubber)
- #341 Add a Breakdown mode to the performance overlay (@roverdrubber)
- #342 Add Hookshot's CRT filters to the present (crt_filter) (@roverdrubber)
- #343 Keep the frame generation display thread from taking the start lock under the present lock (@roverdrubber)
- #345 Run DLSS, FSR 3.1 and FSR 4 as native anti-aliasing at scene_res = full (@roverdrubber)
- #346 Add AMD FSR 3.1 frame interpolation as a way to make frame generation's 2x frames (frame_gen_by = fsr3) (@roverdrubber)
- #347 Pace the audio renderer evenly instead of in bursts after each device period (@Gabinou22s)
- #356 Refill a behind audio queue at once, drop the dead device pacing path (@Gabinou22s)
- #348 Give AMD FSR 3.1 frame interpolation the frames without their HUD, so the HUD is not moved with the scene (@roverdrubber)
- #349 Add Frame generation and its Made by row to the Display tab (@roverdrubber)
- #350 Add a Counter value to the performance overlay: a large FPS number with a small label, after Hookshot's (@roverdrubber)
- #351 Let an upscaler no longer chosen release its images, feature or context after 120 frames (@roverdrubber)
- #352 Count frame generation's set waits, name Auto's flight thumbnails, and correct its pacing comment and docs (@roverdrubber)

### Issues closed or answered by this release

Thank you to everyone who reported a problem. Open ones wait for the reporter to retest.

- #133 Linux audio static with a 1024-frame period (@xenovorx), closed; #272 the same on Windows (@q1209786034-netizen), open, retest asked (#347)
- #152 DLSS not working properly (@Gavury), closed, and #306 DLSS/DLAA is not working (@badman101011), open: the upscaler row stays open on Bananza and DLSS says in the log why it does not run
- #84 (@philneitz) and #155 (@bubblesnout), closed: AMD RDNA 1 and 2 are left out of the default replay threads
- #291 Touchpad button in the controls (@Dimiduh), closed
- #270 FSR 4 / FSR 3.1 temporal upscaler (@MatyMeatBoy), open: fsr3 and fsr4 are on `bananza`
- #285 HD rumble request (@vink2212), open: Switch Pro Controller and Joy-Con only, untested
- #293 AppImage error on step 1 (@Nikoroq), open
- #310 Ingot Isle cave-in missing textures (@shiwntr), open
- #317 FPS drops after a while (@li-beihai), open: one null edge in the job graph builder is guarded
- #318 No AVX2 (@Enzo1383), open
- #335 Polish language (@nario999), open
- #136 Forest Layer snow tunnel crash (@luis-7654), open
- #214 Blurry text at 1440p (@Felitendo), open: `DKB_FONT_SCALE=1`

### Thanks

Pull requests since 0.4.0: roverdrubber, Zorkats, GlebShekovtsov, thatbananaman08, Gabinou22s. Thank you.

---

DKB Recomp 0.4.0: a static recompilation of *Donkey Kong Bananza* (Switch 2) to a native Windows exe with a Vulkan renderer. It is still experimental (0.x). On recent NVIDIA cards players get well past the first level (Glacier, Snow World, Tempest), and it can crash.

**The packages hold no game data, and no release ever will.** You build the game on your own PC from your own dump of **1.0.0 or the 3.2.0 update**, given as a `.nup` file or as ExeFS and RomFS folders. Other updates are not supported.

## What is new since 0.3.0

- **Crashes fixed or guarded.** The Snow World descent device loss on NVIDIA is gone: one light quad (vertex stage ba1dd1e7ae8abb33) is skipped (#196, #197, #264). A crash while loading a save or the title screen at main+0x82fee0 is guarded (#157, #206, #173). The 3.2.0 game now has the layer-change guards 1.0.0 had (#218, as #126), a crash at a layer change when an id cache runs out of nodes is guarded on both versions (#218), and so is a record loop that reads through an unset table index (#229). A Linux crash report now gets as far as the game's function and frames (#148). On AMD's Windows driver the Vulkan recorder thread is off by default, which stopped the device loss for several players (#84, #124, #142, #155, #167, #176).
- **Picture.** The #9 light quads are drawn again, so their particles are back (#62), and the Tower light quads are drawn with a fold of their selector instead of faulting (#220). `scene_res = full` draws the 3D scene at the full output size instead of the 2/3 the game settles at (#210), on 1.0.0 and 3.2.0 (#223). Draws get depth twins like compute passes, so the validation layer is clean in the first level.
- **Performance.** Big render passes are recorded on 4 threads by default from 8 logical processors (not on AMD), dynamic uniform buffers are on by default (not on Intel), and the guest heap is lazy by default on Windows (commit about 9.3 GiB instead of 14.5 GiB). In the heavy -101 scene on an RTX 5060 Ti: about 57.5 fps with a 1% low of about 29 fps (0.3.0: 51.8 and 20.6). The end of a sliced pass no longer waits for its slices (#265), and small VRAM mirror write-backs are copied by one compute dispatch on NVIDIA (#269), and the destructible terrain's geometry-stage storage stays in VRAM instead of being copied back every frame (#268; Hilltop Layer on an RTX 5090: GPU time 16.1 to 12.4 ms a frame), with its bookkeeping kept off the mirror lock (#288). Command recordings reuse cached blocks and hand their buffer over instead of being copied, which removes almost all page faults in the Hilltop Layer (about 470,000 a second before, #289). Many lock and copy cuts in the sliced replay come from contributors (see Thanks).
- **DLC and mods.** Setup installs add-on content from a `.nup` into `aoc-<n>` folders (#183, #111, #258, #263). Loose RomFS mods load from a `mods` folder beside `dkb.exe` (#252).
- **Sound.** A Windows or macOS device that opens with a period that is not a multiple of the renderer's block is reopened with one block per period, which stopped the crackling on a 1024-frame device (#272).
- **High resolutions.** The game's GSys heap grows with the render-target pool, so `scene_res = full` at 3840x2160 and very large outputs no longer crash at start (#244, #279). A guide for 4 GB VRAM cards and laptops: `docs/4gb-vram-optimization.md` (#260).
- **Settings and input.** PlayStation button prompts when a DualShock or DualSense is connected at start (`pad_prompts`, #274). The vertical mouse camera no longer locks during automatic camera moves (#245). Big passes use 6 replay threads from 12 logical processors (#271). NVIDIA DLSS is listed in the upscaler row (#241), and its warm-up waits when another upscaler is chosen at start (#280). DLSS uses preset K except in Ultra Performance, which is cheaper than M on the GPU (`dlss_preset`, #287). `console = 0` hides the console window for frontends such as Playnite (#238). Controller connection handling, DK moving on walls and ceilings above 60 fps (#201), the 3.2.0 version on the title screen.
- **Installer.** It refuses a RomFS folder that holds a RomFS image (#278), compiles only the sources a release ships (#203, #208), recompiles every runtime source when any of them changes (#275), and builds the ultrawide overlays. RecompLauncher can set up and start the Linux and macOS packages, and the release now has a self-contained Linux AppImage, `DKB-Setup-0.4.0-x86_64.AppImage`, beside the `.tar.gz` (#242, #174). `.nx` (NXFormat) dumps are accepted next to `.nup` (#224). CPUs without AVX2 can build the game: setup compiles it for x86-64-v2, which is slower (#231). The shader library prebuild of `--warm-shaders` resumes where it stopped after a restart (#3).
- **Diagnostics.** A crash outside the executable (a driver, a Vulkan layer) is reported with its module, and every crash writes a minidump to `logs/` after the report (#219); keep it on your PC and send it only when asked, privately.
- **Project.** Issue forms ask for the build, game version and OS; a security policy and an accessibility page were added.

## Pull requests in this release

Thank you to everyone who sent code. Each one below is on `bananza` and in this release:

- #209 Slice passes under DLSS, and close the follow-ups of #195 (@GlebShekovtsov)
- #211 Take the texture and mirror locks out of the sliced draws, and reuse a draw's texture descriptors (@GlebShekovtsov)
- #213 Let RecompLauncher set up and start the Linux and macOS packages (@Felitendo)
- #217 Fail a store exclusive after another thread's change and change back (@thatbananaman08)
- #219 Report crashes outside the executable and write a minidump for every crash (@thatbananaman08)
- #220 Draw the faulting light quads with a fold of their selector instead of skipping them, and fix Tower's device loss (@thatbananaman08)
- #222 Run every test from one command, and fix three tests that no longer built or ran (@thatbananaman08)
- #223 Make scene_res draw the 3D scene at its ratio on 3.2.0 too (@wrothgaril)
- #225 Make the DLSS feature ahead of play and once for every input size (@wrothgaril)
- #226 Batch small GPU writebacks and reuse copy buffers (@oshoubber)
- #227 Keep the texture cache generation stable during replay (@oshoubber)
- #228 Batch terrain reset copy commands (@oshoubber)
- #234 Take the texture lock off a guest read's GPU wait, and sort only the new GPU write ranges of the uniform snapshot (@GlebShekovtsov)
- #235 docs: keep the 3.2.0 build status sentence in one tense (@lunar-me)
- #236 docs: fix the verb for the shader census step (@lunar-me)
- #237 docs: add the missing preposition in the cursor-release list (@lunar-me)
- #242 Add selfcontained AppImage build (@Osohow)
- #244 Grow Bananza's GSys heap with the render-target pool, so scene_res and large outputs no longer crash at start (@PorlocK-BR)
- #245 Fix intermittent vertical mouse camera lock in Bananza (@oshoubber)
- #246 Keep DK moving on walls and ceilings above 60 fps (@Zorkats)
- #250 Show the 3.2.0 version on the title screen (@Zorkats)
- #254 Improve controller connectivity, selection and hot-swapping (@shadowlink1174)
- #255 Retire a released pool's descriptors from an index instead of the whole table (@wrothgaril)
- #256 vk_exec_draw: add depth variant origin for mine voxel native GS on RDNA 1/2 (@MatyMeatBoy)
- #257 Take the texture and pipeline locks out of most sliced draws (@wrothgaril)
- #259 Wait for a texture write-back's GPU copy without the texture lock (@wrothgaril)
- #260 perf(docs): add 4GB VRAM optimization guide and config profile (@devadobe11-pixel)
- #261 Prepare Bananza ultrawide overlays during installation (@Marjaperse)
- #262 Find a resolved pool through a per-thread sorted list and a block map (@wrothgaril)
- #265 Let the end of a sliced pass go on without waiting for its slices, by default (@wrothgaril)
- #268 Keep the storage the geometry stage writes in VRAM (@wrothgaril)
- #269 Copy small VRAM mirror write-backs with the compute dispatch by default (@wrothgaril)
- #271 Record sliced passes on 6 threads by default from 12 logical processors (@wrothgaril)
- #273 Wait for an upload batch's fence without cmd_lock (@wrothgaril)
- #274 Show PlayStation button prompts in Bananza when a DualShock or DualSense pad is connected at start (@GlebShekovtsov)
- #275 Compile every runtime source of an install build again whenever its contents change (@GlebShekovtsov)
- #277 Document Steam Deck menu access when the fullscreen hold is enabled (@shadowlink1174)
- #280 Let the DLSS warm-up wait while another upscaler is chosen (@wrothgaril)
- #287 Ask DLSS for preset K outside Ultra Performance, with dlss_preset to choose (@wrothgaril)
- #288 Keep the kept-write bookkeeping off mirror_lock's hot path (@wrothgaril)
- #289 Keep large recording copies in cached blocks and hand the recording buffer over (@wrothgaril)

## Issues resolved

Thank you to everyone who reported a problem, with logs, saves and recordings:

- Device loss on NVIDIA in the Snow World descent and the Glacier rock ride: #197 (@hotcoffeek), #264 (@dudesh34), #196 (@philneitz)
- Device loss or black screen on AMD's Windows driver, with the recorder thread now off by default: #84 (@philneitz), #123 (@BloodThirstyQ), #124 (@TeknaPT), #142 (@beijingpunk), #155 (@bubblesnout), #167 (@jesusen904), #176 (@BoySKAyt), #193 (@stuzukila82-bit), #215 (@thabans), #267 (@350Plays)
- DLC and the update from a `.nup`: #111 (@infinitytom), #183 (@Blinkk000), #258 (@ElBusAlv), #263 (@evylmen)
- Loose mods: #252 (@Caladbolg95)
- Render scale and 3D resolution: #149 (@formandfunctionlab), #210 (@dancaiman), #279 (@Dimiduh)
- DLSS: #152 (@Gavury), #241 (@thecharted135-sketch)
- Performance in heavy scenes: #154 (@patrickfuly), #156 (@supermahdi-enjoyer), #194 (@li-beihai), #207 (@solidaytug), #230 (@zachcristallguy-netizen), #284 (@wrothgaril)
- Above 60 fps, climbing walls: #201 (@XDM-Inc)
- Raw mouse camera: #177 (@pritomroyreal-cloud)
- Controllers: #253 (@shadowlink1174)
- Installer: #203 (@bika66), #208 (@Sapo2099), #278 (@sonic-un), #138 (@asluan287-gif)

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

- NVIDIA GPU with a recent driver (tested on an RTX 5060 Ti and an RTX 3070 Ti Laptop). AMD and Intel are experimental: on AMD's Windows driver the device loss most players hit is fixed (recorder thread off), and RADV on Linux can still hang (#99, #122, #198, #204). Intel Arc: #3.
- 16 GB of RAM plus a large page file (the process commits about 9 GB), 32 GB is comfortable.
- A controller is recommended.

## Known issues

- GPU hangs under RADV on Linux (#99, #122, #198, #204), and a driver fault on Intel Arc (#3).
- Crashes reported without a reproduction here: #126 (Lagoon to Hilltop tunnel, retest asked), #136 (Forest snow tunnel landing: needs a guard on a call, after this release), #148 (Linux, after the first boss: the next log will name the function).
- Black shards where salt or acid meets slime (#139, #266).
- Text is soft at 1440p and above: the game's fonts are drawn at a fixed size (#214).
- Heavy scenes can still fall under 60 fps, and the first visit to an area can stutter.
- Checkerboard textures (#5), #112 and unrecorded NVN 2 calls (#6).

## Reporting a bug

Open an issue with the log (`dkb.log`), the build line at its top (`dkb 0.4.0+<commit>`), your GPU and driver, and what you did before it failed. **Never share generated files, keys or screenshots that show game content.**

Roadmap: #10.
