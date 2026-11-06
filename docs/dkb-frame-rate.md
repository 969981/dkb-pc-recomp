# Experimental Bananza frame timing

Bananza 1.0.0 automatically adjusts game timing to the existing frame-rate
selector, including its monitor-rate choice. The default cap remains 60 FPS.
Changes apply without restarting or enabling a separate setting. TotK retains
its manual real-time-clock toggle; Bananza does not show that toggle.
No new frame-rate choices are added. Numeric caps in configuration accept 30
through 360; this is an experimental range,
not a claim that all rates or scenes have been validated.

The host measures presentation intervals using the existing five-frame average.
The Bananza controller receives a rounded FPS value, while seven clock producers
receive the precise floating-point step. Their existing pause, time-scale and
seconds-conversion logic remains in place. The maximum step is 1/15 second;
long stalls can still slow simulation. No rendering interpolation or frame
generation is added. CPU and GPU performance determine the achievable rate.

## Validation

- Patch generation checks all original instruction words against base 1.0.0.
- `python tests/dkb_frame_rate_test.py --cc <clang>` checks the lifted replacement
  instructions on synthetic registers, stack and controller memory, including
  fractional rates, at O0 and O2: 9,688 cases per build, zero failures.
- A local Unicorn check independently executes all nine replacement sequences
  at twelve rates: 108 cases, zero failures.
- Windows, Ryzen 7 9800X3D, RTX 5090, 240 Hz display: isolated save loads and scripted cave gameplay at 30 and 60
  FPS, and with 120 and 240 caps, exited successfully. The 240-cap run reached
  240 FPS on the title screen and approximately 100–170 FPS in the tested cave
  views. A measured eight-second gameplay interval accumulated 7.997 seconds
  in the camera's native clock with the precise clock patches.
- Targets were tested through 240 FPS to match the available display; higher
  rates were not validated. Gameplay remains performance-limited: the tester
  reports difficulty sustaining above approximately 120 FPS on this system.
  The short automated observations above do not establish a performance gain.
- The clean player build passes `--selftest` and `--probe-cfg`.
- The automatic-timing revision completed a 72-second session without an opt-in
  flag, including with the old `REALTIME=0` setting. Caps switched through 60,
  120, 240, 30, 45, 60, monitor rate (239.984 Hz), and back to 60 without a
  restart. This background run was performance-limited to roughly 30–35 FPS in
  the cave, so it validates live configuration and timing rather than high-FPS
  performance. A late sample reported simulation/wall-time speed of 0.998.
- One 65-second session switched 60 → 120 → 240 → 30 → 45 FPS through the
  live settings API without restarting and exited successfully. The observed
  camera clock settled to 2 at 30 FPS and 1.333333 at 45 FPS.

This is not fully qualified arbitrary-frame-rate support. A one-second full-stick
camera turn accumulated about 139.6 degrees at 60 FPS and 147.5 degrees in the
higher-rate run. Input sampling and the camera's frame-based smoothing need
further isolation. Jumping and movement were exercised, but jump trajectories,
destruction physics, cutscenes, audio synchronization and long-session stability
have not been established equivalent across rates. No test establishes sustained
240 FPS gameplay in the cave.

## Related work

[Bananza PR #80](https://github.com/Lucas-blip-png/dkb-pc-recomp/pull/80) is an
existing proposal for smoothing simulation at 10–60 FPS. Its controller location
helped locate the original timing path; this experiment uses the repository's
switchable patch table and precise host clock instead of modifying NSO loading.
No implementation code from that PR was copied.

[TotK's frame timing](https://github.com/valentderah/totk-pc-recomp/blob/main/runtime/frame_rate.c)
provides the inherited host pacer. Its current upstream implementation documents
physics failures with steps shorter than 1/240 second. Those TotK-specific limits
are a reference for testing, not evidence of Bananza's safe limits.
