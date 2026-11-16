# FidelityFX SDK (FSR 3.1 upscaler part)

Unmodified copy of AMD's FidelityFX SDK v1.1.4, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`
(https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), taken from Hookshot's `third_party/fidelityfx-sdk`
(https://github.com/NotRexed/Hookshot @ `fab8cb37641714bd839ae83c2a7bc582bedc4d70`). License: MIT, `LICENSE.txt`.

What is here: `include/FidelityFX/gpu/` (the core headers, `spd/`, `fsr3upscaler/`) and `shaders/fsr3upscaler/` (the
passes, without RCAS: the renderer does not sharpen with it). Frame interpolation, optical flow and FSR 1 are left out.
`tools/build_shaders.py` compiles the passes (FSR3_VARIANTS) into `runtime/fsr3_*_spv.h`. Nothing here is edited.
