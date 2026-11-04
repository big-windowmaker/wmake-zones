# Desempenho

Medições de 2026-11-04 num Xvfb 1920×1080 com o Window Maker 0.96
(`tests/perf-x11.sh`) e no wlmaker aninhado num Xvfb pelo backend X11 do
wlroots com o renderizador pixman (`tests/wayland-functional.sh` e os
roteiros da mesma pasta). Valores do ambiente de teste, não de hardware real.

## X11 (serviço `wmake-zones`)

| Medida | Valor |
|---|---|
| PSS em repouso, overlay nunca mostrado | 0,71 MiB |
| PSS depois do primeiro overlay (Xft e fontconfig carregados) | 2,69 MiB |
| PSS depois de mais 20 e de mais 60 ciclos (mosaico liga/desliga, troca de layout, prévia) | 2,69 MiB e 2,69 MiB |
| Memória por janela gerenciada | ~0,6 KiB (10 → 50 janelas: +23 KiB) |
| Acordadas em 10 s de repouso | 0 (antes e depois dos ciclos) |
| Tiques de CPU em 10 s de repouso | 0 |
| Mosaico: cálculo e pedidos, 10/30/50 janelas | 3,3 / 1,2 / 2,0 ms |
| Overlay: primeira vez, com carga das fontes | 12,7 ms |
| Encaixe: pedido ao Window Maker | 32 µs |
| Vazamentos (ASan + LSan, sem supressões, 3 rodadas, cenário completo) | 0 relatórios |

Os tempos de mosaico e de encaixe medem o lado do wmake-zones (cálculo e
envio dos pedidos); a janela chega ao lugar quando o Window Maker e o
aplicativo atendem.

## Wayland

| Medida | Valor |
|---|---|
| PSS do wlmaker com o módulo de zonas / sem (mesmo commit, 3 rodadas) | 9,47 / 9,42 MiB (+48 KiB, dentro do ruído) |
| Dockapp `wmake-zones-wl` | 0,65 MiB PSS, 0 acordadas em 10 s |

## Preferências

`wmake-zones-prefs` (GTK 3): 15,3 MiB de PSS **só enquanto aberto**; nada
dele fica residente.

## Como o repouso fica em zero

* Nenhum timer em repouso: `poll()` sem timeout. Timers só durante um
  arrasto (atraso do overlay), numa prévia (1,8 s) e para agrupar o
  mosaico (30–40 ms).
* Com Zones e Auto Tiling desligados, o serviço nem pede eventos de botão;
  sem nada ligado e sem `KeepRunning`, ele nem roda (o autostart sai na
  hora).
* Teclas brutas só durante um arrasto; a posição do ponteiro é lida uma vez
  por lote de eventos.
* O overlay é criado ao mostrar e destruído ao esconder; redesenha só
  quando a zona destacada muda.
* A lista de ladrilhos do Dock vem dos eventos de estrutura, sem varrer a
  árvore de janelas.
* O arquivo de configuração só é escrito por ação do usuário.

## Não medido

* Latência ponta a ponta em hardware real (do clique ao pixel).
* Consumo com dois ou três monitores físicos e hotplug.
* Impacto em jogos e aplicativos em tela cheia (eles ficam fora por
  construção: tela cheia nunca é encaixada nem entra no mosaico).
