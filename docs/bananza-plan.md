# DK Bananza PC — plano

Objetivo: *Donkey Kong Bananza* (Switch 2, base 1.0.0) jogável no Windows como recompilação estática, no molde do
[TotK Recomp](https://github.com/valentderah/totk-pc-recomp) (MIT), que é a base. Sem arquivos do jogo no repo:
o usuário fornece o dump do próprio cartucho e o build acontece na máquina dele.

Entrada conhecida (01/11/2026): `exefs` base = `main` (md5 f68752f5…, o mesmo binário do BinSync do
GRAnimated/DKBananza) + `sdk`, sem `subsdk0`; update `458752` com outro layout. RomFS ~19 GB.

## Fases

Cada fase termina num marco verificável. A ordem é a do caminho crítico; itens marcados (∥) andam em paralelo.

### F0 — Base e lifter (CPU)
- [x] Lifter do TotK roda no Bananza: 133.806 funções no `main`, 46.255 no `sdk`.
- [x] Censo de instruções novas do ARMv8.3: `retab` (PAC, 69,8 mil), `ldapr{,b,h}` (21,1 mil).
- [x] Censo completo do jogo inteiro (geração sem `--limit`): só `retab`, `ldapr*` e 5 PAC de dados no `sdk`.
- [x] Suporte no lifter (commit 3941578): `retab`/`retaa`/`braa`/`blraa`… como desvio sem autenticação; `ldapr*` como `ldar*`;
      hints PAC (`pacibsp`, `autibsp`…) como NOP. Teste diferencial contra o Unicorn para cada classe nova.
- [x] Repo local `repo/` (branch `bananza`) a partir do TotK Recomp 0.2.8, histórico mantido.
- [ ] Publicar no GitHub (Lucas-blip-png/dkb-pc-recomp) com README honesto.
- **Marco:** `main` + `sdk` inteiros geram C sem `unsupported` e compilam.

### F1 — Sistema (HLE do Horizon do Switch 2)
- [x] Loader: NSO do Switch 2 (RELR, MOD0), TLS, layout de memória; `main.npdm` (capacidades, serviços).
- [x] Reaproveitar o host do TotK (threads, IPC, fs/RomFS, time, account, applet) e logar toda chamada desconhecida.
- [x] Serviços/SVCs novos do Switch 2 conforme aparecerem (o firmware do Switch 2 é Horizon 20+).
- **Marco:** o jogo sai do `nnMain`, monta o RomFS e chega na primeira chamada de NVN.

### F2 — NVN 2 (API gráfica) com renderizador nulo
- [x] Engenharia reversa do bootstrap e da tabela de procs do NVN 2 a partir do próprio `sdk`/`main`
      (o driver é linkado no jogo, como no Switch 1). Comparar com o NVN 1 do TotK (`runtime/nvn_*`).
- [x] Front end NVN 2 que grava os comandos; back end nulo (sem GPU) só para o jogo avançar quadros.
- **Marco:** o loop do jogo roda (quadros contando, áudio tocando, input chegando) sem imagem.

### F3 — Shaders Ampere → SPIR-V (o item mais difícil)
- [ ] Decodificador do SASS Ampere (SM86). Fontes de conhecimento: o compilador NAK do Mesa (emite Turing/Ampere;
      MIT), o PR de frontend Ampere do Eden (#4588, WIP, 30/10/2026) e o tradutor Maxwell do Ryujinx/TotK como
      molde da IR.
- [ ] Tradutor para SPIR-V, cache de shaders na instalação (como o `prepare_shaders.py` do TotK).
- **Marco:** os shaders do menu inicial traduzem e validam (`spirv-val`).

### F4 — Back end Vulkan
- [ ] Portar o back end Vulkan do TotK para o modelo do NVN 2: texturas (formatos, deswizzle de blocos), pools de
      memória, sincronização, render targets, compute.
- **Marco:** a tela de título aparece.

### F5 — Jogável
- [ ] Gameplay até a primeira fase, correção de crashes e de bugs visuais (o voxel é compute pesado).
- [ ] Áudio (renderer do Switch 2), input com teclado/controle, saves.
- [ ] Instalador nos moldes do TotK (extração do dump do usuário, geração e build locais).
- **Marco:** dá para jogar do começo até a primeira fase sem crash.

### Paralelo (∥)
- (∥) Símbolos no GRAnimated/DKBananza (nerves, nomes via BinSync) — ajudam a depurar F1–F5. PR #1 aberto.
- (∥) Acompanhar Eden/oboromi/NeXo2 e qualquer projeto de NVN 2; citar e creditar o que for usado.

## Regras
- Nada do jogo no git: nem exefs, romfs, C gerado, shaders extraídos ou capturas com assets.
- Licenças: só código MIT/BSD/zlib no que é distribuído; GPL (Unicorn) só em teste.
- Commits sem assinatura do Claude; PRs pequenos e com números honestos.
- Progresso e decisões em `RELATORIO.md`.
