# Shaders do Bananza (base 1.0.0)

- RomFS `Shader/`: `SilverStarMaterial...Nin_Ounce_NVN2.bfsha.zs` (12,2 MB zstd; o grosso), `Project...release.sarc.zs`
  (660 KB), `PhotoFilter...bfsha.zs` (14 KB). "Ounce" = codinome do Switch 2.
- `bfsha` = contêiner `FSHA` com `BNSH` dentro (e `grsc`): mesma família de formato do Switch 1; o que muda é o código
  de GPU (Ampere) dentro do BNSH.
- `Lib/sead/primitive_renderer/primitive_drawer_nvn_shader.bin` (7168 B) e `..._nvn2_shader.bin` (6912 B): o MESMO
  shader compilado para NVN 1 (Maxwell) e NVN 2 (Ampere). Pedra de Roseta para o decodificador Ampere: o tradutor
  Maxwell do TotK já entende o primeiro. Os dois têm o magic de controle NVN `0x98761234` em +0x10; o campo seguinte é
  `0x01` (NVN 1) e `0xC9` (NVN 2).
