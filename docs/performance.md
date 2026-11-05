# Desempenho

Medições da versão 0.3.0, de 2026-11-05, num Xvfb 1920×1080 com o Window Maker 0.96
(`tests/perf-x11.sh`) e no wlmaker aninhado num Xvfb pelo backend X11 do
wlroots com o renderizador pixman (`tests/wayland-functional.sh` e os
roteiros da mesma pasta). Valores do ambiente de teste, não de hardware real.

## X11 (serviço `wmake-zones`)

| Medida | 0.2.0 | 0.3.0 |
|---|---|---|
| PSS em repouso, overlay nunca mostrado | 0,73 MiB | 0,77 MiB |
| PSS depois do primeiro overlay (Xft e fontconfig carregados) | 2,79 MiB | 2,93 MiB |
| PSS depois de mais 20 e de mais 60 ciclos (mosaico liga/desliga, troca de layout, prévia) | 2,79 / 2,79 MiB | 2,93 / 2,93 MiB |
| Acordadas em 10 s de repouso | 0 | 0 |
| Tiques de CPU em 10 s de repouso | 0 | 0 |
| Mosaico: cálculo e pedidos, 10/30/50 janelas | 2,6 / 5,5 / 5,2 ms | 2,5 / 5,2 / 6,6 ms |
| Overlay com as zonas: primeira vez, com carga das fontes | 11,4 ms | 11,4 ms |
| Overlay com o painel de layouts (fluxo padrão da 0.3) | — | 2,9 ms |
| Encaixe: pedido ao Window Maker | 34 µs | 25 µs |
| Histórico do desfazer, 20 passos de uma janela (calculado, não medido: 2 × 21 passos de 136 B + 20 × 4 × 56 B) | — | ~10 KiB |
| Vazamentos (Valgrind Memcheck, teste funcional inteiro) | — | 0 bytes perdidos, 0 erros |

Em repouso, ~22 KiB a mais que a 0.2.0 (770 contra 748 KiB): o código
novo e as duas pilhas do histórico. Depois do primeiro overlay, ~0,14 MiB
a mais: a fonte em negrito do título do painel de layouts.

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

## Tela de bloqueio (big-wm-lockscreen, mesma rodada)

| Medida | Valor |
|---|---|
| Fundo desfocado (captura 1600×900, redução, 3 passadas de caixa, escurecer, PNG 0600) | 29 ms |
| Fundo do papel de parede: primeira vez (JPEG pelo glycin) / do cache | ~270 ms / ~60 ms |
| Do `big-wm-lock` até a tela bloqueada no wlmaker (gtklock) | ~210 ms |

## Não medido

* Latência ponta a ponta em hardware real (do clique ao pixel).
* Consumo com dois ou três monitores físicos e hotplug.
* Impacto em jogos e aplicativos em tela cheia (eles ficam fora por
  construção: tela cheia nunca é encaixada nem entra no mosaico).
