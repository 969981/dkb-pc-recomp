# AMD FSR API headers (FidelityFX SDK v2.4.0)

Unmodified MIT-licensed headers from AMD's FidelityFX SDK, tag `v2.4.0`
(<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>, `Kits/FidelityFX/api/include` and
`Kits/FidelityFX/upscalers/include`). See `LICENSE.md`.

Only the headers are compiled in (`runtime/vk_fsr4.c`). AMD's signed
`amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` (from `Kits/FidelityFX/signedbin` of the same release)
are not vendored: they are loaded at run time, from the folder of `dkb.exe`, when `upscaler = fsr4`
is chosen (copied from Hookshot, commit fab8cb37641714bd839ae83c2a7bc582bedc4d70).
