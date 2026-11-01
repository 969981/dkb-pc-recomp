# Shaders do Bananza (base 1.0.0)

- RomFS `Shader/`: `SilverStarMaterial...Nin_Ounce_NVN2.bfsha.zs` (12,2 MB zstd; o grosso), `Project...release.sarc.zs`
  (660 KB), `PhotoFilter...bfsha.zs` (14 KB). "Ounce" = codinome do Switch 2.
- `bfsha` = contêiner `FSHA` com `BNSH` dentro (e `grsc`): mesma família de formato do Switch 1; o que muda é o código
  de GPU (Ampere) dentro do BNSH.
- `Lib/sead/primitive_renderer/primitive_drawer_nvn_shader.bin` (7168 B) e `..._nvn2_shader.bin` (6912 B): o MESMO
  shader compilado para NVN 1 (Maxwell) e NVN 2 (Ampere). Pedra de Roseta para o decodificador Ampere: o tradutor
  Maxwell do TotK já entende o primeiro. Os dois têm o magic de controle NVN `0x98761234` em +0x10; o campo seguinte é
  `0x01` (NVN 1) e `0xC9` (NVN 2).

## Controle NVN 2 x NVN 1 (sead primitive_drawer, estágio 0; tools/ctlcmp.py)
Layouts totalmente diferentes; o NVN 2 é um formato novo (versão +0x04 = 0xC9 = 201; NVN 1 = 1).
NVN 2, primeiros campos lidos:
- +0x00 0x98761234 magic; +0x04 0xC9 versão; +0x08 0x0B (?); +0x0C 0x000E0170 (?)
- +0x18 0x00200004, +0x1C 0x00288000 (?)
- +0x20..+0x2C hash de 128 bits
- +0x30 0x930 = tamanho do controle (o estágio 1 começa em 0x10 + 0x930 = 0x940)
- +0x38 0x400 = tamanho do código (vértice 0xD00..0x1100); +0x40 e +0x44 também 0x400
- +0x4C 0x11 (?); +0x98 0x930 (repetido)
Código Ampere começa direto (sem o cabeçalho 0x12345678 do NVN 1). Instruções de 128 bits (SM70+).
Plano F3: (1) decifrar o resto do controle com mais pares de shaders; (2) decodificador SM86 a partir do
`refs/mesa/src/nouveau/compiler/nak/sm70_encode.rs` (MIT); (3) entrar no IR do tradutor Ryujinx que o TotK usa
(tools/vendor/ryujinx, MIT) como um frontend novo, reaproveitando o gerador SPIR-V.

## Corpus do jogo (01/11, 2.656 estágios via DKB_SHADER_DUMP; tools/ctlstats.py, tools/sphstats.py)
- Controle NVN 2 do jogo: 0xD8 bytes (2.654); o do sead tem 0x930. 96 estágios trazem 0 em +0x30 (variante, a ver).
- Constantes: +0x00 magic, +0x04 0xC9, +0x0C 0x000E0170 (alvo? SM86), +0x10/+0x14 0, +0xA4 0x1000000.
- +0x20..+0x2C hash de 128 bits (1.907 distintos). +0x30 = tamanho do controle (repetido em +0x98, +0xC4, +0xC8).
- +0x38 = tamanho do código; +0x40/+0x44 parecidos (= código em 1.358 casos): talvez código sem constantes.
- +0x4C pequeno (0x24..0x48): candidato a contagem de registradores. +0x54/+0x60 múltiplos de 0x1000.
- +0x08 = 0xB (2.626) ou 0x7 (30): NÃO é o estágio.
- **Estágio vem do SPH no código**: gráficos começam com SPH v4 (32 palavras = 0x80 bytes, NAK sph.rs), word0 bits 0-4
  sph_type (1 VTG, 2 PS), 5-9 versão (4), 10-13 shaderType (1 vertex, 5 pixel). 1.460 vertex, 1.138 pixel, 58 compute
  (compute sem SPH). Instruções começam em +0x80 nos gráficos.
- Próximo: decodificador SM86 validado contra o nvdisasm (CUDA toolkit, `-b SM86`), usando sm70_encode.rs do NAK.
- nvdisasm SM86 sobre o VS do sead (código após o SPH): ALD.96 a[0x80] / ALD a[0xa0] → duas matrizes 4x4 em c[0x4] e
  c[0x3] (FMUL/FFMA/FADD .FTZ) → AST.128 a[0x80] (cor), AST.128 a[0x70] (posição), EXIT. Atributos no layout clássico
  (a[0x70] = posição, a[0x80+] genéricos): o mapeamento do Ryujinx para Maxwell deve servir.

## Integração (f080197 em diante)
- Caminho: nvnProgramSetShaders → runtime/nvn2_lower.cpp (Eglantine) → controle NVN 1 sintético → totk_xlate.
  Offline (`tools/dkb_lower_test.cpp` + `totk_xlate jobs.json`): 2.597/2.656 rebaixam, 2.564 viram SPIR-V.
  Fechar o buffer com um grupo de EXIT Maxwell foi o que tirou 855 IndexOutOfRange do decodificador do Ryujinx.
- Banco do driver c0: gráficos usam o MESMO layout do NVN 1 (VS sbuf 0x110, PS 0x510); compute é outro
  (0x10-0x2c dimensões?, sbuf 0x60+16r até 0x1e0). Texturas: handles combinados em c2 0x20+8s, como no NVN 1.
- Restam: 33 "GlobalMemory" no Ryujinx (LDG/STG sem base em cbuf), 59 destinos de BRA desalinhados (captura lê
  além do fim).
- Do NX2_NOTES do Eglantine (a fazer): banco 24 = bloco de parâmetros de blit do console (sintetizar da área de
  render); ASTC com swizzle de GOB do Switch 2 (o deswizzle do TotK é o do Switch 1); mesh/task shaders
  (ISBEWR/ISBERD); texturas virtuais 8192x8192x68 ASTC (alocar esparsas); IPA com modos de interpolação.
