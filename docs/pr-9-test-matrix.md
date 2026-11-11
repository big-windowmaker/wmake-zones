# Matriz de testes do PR #9

Ambiente: Manjaro, GCC 16.2.1, meson 1.12.1, Xvfb/Xorg 21.1, Window Maker
0.96.0, Xephyr 21.1.24 (extraído do pacote, assinatura conferida, sem
instalar), xdotool 4.20260303.1, wlmaker 1:0.8.r80.g7481afe-33 (com as
zonas). Sessões isoladas (Xvfb/Xephyr próprios, HOME temporário, sem o
barramento do sistema). Sem Clang e sem cppcheck na máquina: não rodados.

| Teste | main | PR 394be74 | PR corrigido |
|---|---|---|---|
| compilação (gcc, avisos padrão) | PASS, 0 avisos | PASS, 0 avisos | PASS, 0 avisos |
| gcc `-Dwarning_level=3 -Dwerror=true` | — | — | PASS |
| engine | PASS | PASS | PASS |
| metainfo | PASS | PASS | PASS |
| x11-functional | 56/56 | 56/56 | 56/56 |
| x11-multimonitor (Xephyr) | SKIP sem Xephyr | SKIP | 13/13 |
| x11-dockapp | 17/17 | **FAIL** (3: cores antigas) | 26/26 |
| x11-dockapp com o teste novo | — | **FAIL** (6 bordas) | 26/26 |
| x11-dockapp-noshape | — | — | 26/26 |
| dockapp com ASan+UBSan (com e sem Shape) | — | — | 26/26, sem relatórios |
| wayland-functional (wlmaker aninhado) | 51/51 | — | 51/51 |
| wayland-functional com wlmaker sem zonas | — | — | SKIP declarado |
| shellcheck `tests/x11-dockapp.sh` | limpo | — | limpo |
| perf-x11 (repouso, mosaico, overlay, ciclos) | ver performance | — | equivalente |
| dockapp parado 10 s (3 rodadas) | — | 0 ticks, 0 trocas | 0 ticks, 0 trocas |

## Comportamento

| Verificação | Esperado | PR 394be74 | Corrigido |
|---|---|---|---|
| popup de layouts | miniaturas com contorno | sem contorno | com contorno |
| Z em x=29 do ladrilho | Z | ia para M | Z |
| M em x=44 | M | nada | M |
| ◀ no 1º pixel, ▶ no último, última linha | a tecla | Window Maker | a tecla |
| sem X Shape | sem amarelo | 772 px amarelos | igual ao com Shape |
| margens da tela | iguais | 2/4 px | 2 px |
| clique direito na borda | menu do Window Maker | PASS | PASS |
| arrastar para o Dock | entra no Dock | PASS | PASS |
| clique sem arrastar | troca o modo | PASS | PASS |

Interativo em sessão real (não aninhada): não feito (sessão de trabalho do
desenvolvedor). Clip e gaveteiro: cobertos só pelo encaminhamento ao Window
Maker (o mesmo do Dock), não arrastados em teste.
