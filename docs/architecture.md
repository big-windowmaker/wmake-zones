# Arquitetura

```
                     ~/.config/wmake-zones/wmake-zones.conf  (INI, GKeyFile)
                        ▲            ▲                 ▲
                        │            │                 │
   ┌────────────────────┴──┐  ┌──────┴──────────┐  ┌───┴──────────────────┐
   │ wmake-zones (X11)     │  │ wlmaker + 0104- │  │ wmake-zones-prefs    │
   │ serviço + dockapp     │  │ src/zones.c     │  │ (GTK 3, sob demanda) │
   │ Xlib, XI2, XRandR,    │  │ 0116 zones.c:   │  │ seções + editor      │
   │ XShape, Xft           │  │ overlay, Zones  │  └──────────┬───────────┘
   └──────────┬────────────┘  └───────┬─────────┘             │
              │ núcleo (libwmake-zones-engine.a, sem X11/Wayland)
              │ wz-layout · wz-tile · wz-config · wz-history · wz-theme · wz-ipc
              ▼                       ▼                        ▼
     $XDG_RUNTIME_DIR/wmake-zones/x11-<n>.sock   wayland-<display>.sock
              ▲                       ▲
              └──── wmake-zones <comando>, wmake-zones-wl (dockapp) ────┘
```

## Peças

| Peça | Papel | Quando roda |
|---|---|---|
| `src/engine` | Geometria (árvore de layout, cálculo exato), mosaico, foco espacial, histórico (desfazer/refazer), configuração e modos, tema do Window Maker, IPC | Ligado a todos os programas |
| `wmake-zones` | Serviço X11: arrasto, painel de layouts, overlay, seletor, mosaico, atalhos, dockapp, socket | Com Zones (padrão) ou Auto Tiling ligados, ou `KeepRunning` |
| `wmake-zones-prefs` | Preferências e editor (GTK 3) | Só quando aberto |
| `wmake-zones-wl` | Dockapp nativo do wlmaker | Quando posto no Clip/Dock |
| wlmaker (patches 0104–0116) | Zonas, painel de layouts, seletor, mosaico, regras, histórico, ação `Zones`, socket | É o compositor |

Um processo por função. No X11, o dockapp mora dentro do serviço (um
processo só); as preferências, que carregam o GTK, nunca ficam residentes.

## Núcleo

* **Layout** = árvore de divisões. Folhas são zonas; nós `H` dividem lado a
  lado, `V` empilham, com pesos. Forma textual: `H(3:V(3"Editor",1),2)`.
* **Cálculo exato**: a divisão usa arredondamento cumulativo, então as zonas
  cobrem a área útil sem buracos nem sobreposição; o gap interno tem sempre
  a largura pedida (a metade de cada lado, somando exato); a margem externa
  é tirada antes. Célula menor que o gap encolhe o gap, não a zona.
* **Números independentes do locale**: o pt_BR usa vírgula decimal, que
  aqui separa itens; leitura e escrita próprias (`wz_strtod`).
* **Mosaico**: cada algoritmo monta uma árvore e reaproveita o mesmo
  cálculo; o BSP guarda a árvore (folha = janela). Espiral e subdivisão
  param de dividir abaixo de 64 px e empilham o resto.
* **Foco espacial**: `wz_focus_pick` escolhe a janela mais natural numa
  direção somando o vão ao longo do eixo, metade da distância entre os
  centros, o dobro do vão perpendicular e o desvio dos centros quando as
  faixas não se cruzam (um quarto dele quando se cruzam, para desempatar).
  Determinístico: empate fica com a primeira da lista.
* **Histórico**: duas pilhas de tamanho fixo (profundidade + 1), passos
  com a geometria e os estados das janelas tocadas e até quatro chaves da
  configuração; o backend aplica o passo e monta o inverso (refazer). Uma
  janela fechada some de todos os passos; um passo vazio nunca expulsa um
  antigo.
* **Modos**: Zonas, Mosaico (as zonas continuam para as janelas fora do
  mosaico) e desligado, com o último modo lembrado (`LastMode`).
* **Restrições**: `wz_fit` centraliza quando o máximo do aplicativo é menor
  que a célula; quando o mínimo não cabe, é conflito (no mosaico a janela
  flutua; nas zonas, o mínimo do aplicativo vence).

## Prioridades das decisões

1. O que o Window Maker/wlmaker exige (tamanho fixo, tela cheia, diálogos)
2. O que o usuário fez agora (arrastar, Esc, flutuar com Super+Ctrl+F)
3. Regras por aplicativo (`[Rule ...]`)
4. Layout/mosaico ativo do monitor e da área de trabalho
5. Padrão: flutuante (as zonas só agem quando o usuário solta a janela numa)

## Recuperação

* Se o serviço X11 cai, o Window Maker não percebe: janelas ficam onde
  estão, não há grab nem overlay (o overlay é destruído ao esconder e é
  override-redirect com entrada vazia).
* O `WindowSnapping` do wmaker desligado pelas zonas é marcado em
  `[State] RestoreWindowSnapping` e volta ao desligar, no próximo login com
  tudo desligado, ou com `wmake-zones --reset`.
* A geometria anterior ao encaixe/mosaico fica na própria janela
  (`_WMAKE_ZONES_RESTORE`), então sobrevive a um reinício do serviço.
* No wlmaker, sem o núcleo na compilação, `zones.c` vira stubs e nada muda;
  com ele, uma falha ao abrir o socket só registra um aviso.
