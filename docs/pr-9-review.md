# Revisão do PR #9 (dockapp X11 com visual LCD)

PR: https://github.com/big-windowmaker/wmake-zones/pull/9, de @deleterium,
branch `deleterium:deleterium`. Revisado a partir do HEAD `394be74` (quatro
commits: `6505afd`, `592138c`, `8a38270`, `394be74`), base `main` `56364ad`.

## Objetivo

Um dockapp X11 mais limpo, no estilo LCD: ladrilho de 60x60 (o Window Maker
centra no quadro de 64), só a tela e as teclas desenhadas (o resto mostra o
ladrilho do Dock pelo X Shape), bordas de 1 px, miniaturas em ciano, Z e M
numa tela LCD própria.

## Arquivos

- `src/x11/wzx-dockapp.c`: desenho do ladrilho, miniaturas, áreas das teclas,
  máscara X Shape.
- `src/x11/wzx-overlay.c`: `wzx_draw_bevel_1px()`; includes com
  `../engine/` (sem efeito no meson, que já põe `src/engine` no caminho).
- `src/x11/wzx.h`: o protótipo e os mesmos includes.

Nenhum arquivo do Wayland (`src/wayland/`), do núcleo ou dos workflows.

## Alertas do CodeRabbit

| Alerta | Verificação | Resultado |
|---|---|---|
| `draw_mini()` desenha o contorno em `face` | popup de layouts capturado | **confirmado**: miniaturas do popup sem contorno |
| cliques nas bordas vão ao Window Maker | teste das bordas | **confirmado**: o primeiro pixel de ◀, o último de ▶ e a última linha da fileira eram da "borda" (3 px) do ladrilho de 60 |
| regiões de Z e M (x=29, x=44) | desenho × `row_btn` | **confirmado**: Z desenhado 15..29, clicável 14..28; M desenhado 30..44, clicável 29..43 |
| amarelo sem X Shape | `XShapeCombineRectangles` sem efeito | **confirmado**: 772 pixels amarelos |
| documentação das funções | código | comentários acrescentados |
| título do PR ("Deleterium") | — | sugerido um descritivo no review |

## Impacto

- X11: só o dockapp. Daemon, overlay, mosaico, arrasto: inalterados (suítes
  funcional e multimonitor passam).
- Wayland: nenhum código do PR roda lá; o teste funcional passa igual no
  `main` e no PR. O dockapp nativo do wlmaker continua com o visual
  anterior: a paridade fica como proposta (port do desenho para o
  `wzw-dockapp.c`, em Cairo), fora deste PR.

## Riscos e soluções

- Shape: o input também segue a forma; as frestas entre as teclas (1 a 3 px)
  passam a ser do Window Maker (arrastar, menu dele). Comportamento
  aceitável e coerente com a borda.
- Servidor sem X Shape: fallback por `ParentRelative` (a janela já era
  criada assim) e cópia só das partes desenhadas; testado com
  `WZX_NO_SHAPE=1`, pois o X.Org atual não deixa desligar a extensão.
- Testes do dockapp presos às cores antigas: atualizados sem perder
  cobertura (de 17 para 26 verificações, e a variante sem Shape).
