# Veredito do PR #9

| Item | Situação |
|---|---|
| Compilação | PASS (gcc, 0 avisos; `-Dwarning_level=3 -Dwerror=true` também) |
| Sanitizers | dockapp com ASan+UBSan, com e sem X Shape: 26/26, sem relatórios; núcleo na CI |
| X11 | x11-functional 56/56, x11-multimonitor 13/13 (Xephyr), x11-dockapp 26/26, x11-dockapp-noshape 26/26 |
| Wayland | wayland-functional 51/51 local (wlmaker 33 aninhado); na CI, SKIP declarado (sem wlmaker no contêiner) |
| CI | run 43056138740 no HEAD `827c151`: success, com o dockapp e os vários monitores rodando (antes eram SKIP em toda execução, também no `main`) |
| Desempenho | igual ao `main`: dockapp parado 10 s com 0 ticks e 0 trocas de contexto; perf-x11 equivalente |
| Problemas do review | os quatro alertas do CodeRabbit confirmados e corrigidos (ver `pr-9-fixes.md`) |

## Pendências (não bloqueiam)

- Teste interativo numa sessão X11 real e no Clip/gaveteiro (o
  encaminhamento ao Window Maker é o mesmo do Dock, que passa).
- Clang e cppcheck: não disponíveis na máquina do review.
- Paridade visual do dockapp nativo do wlmaker (`wzw-dockapp.c`) com o
  visual LCD: proposta para um PR próprio.

## Recomendação

APPROVE, condicionado à CI do HEAD final do PR. Depois do código validado
(`146fe2d`) só mudaram documentos, o workflow e o registro do teste do
Wayland (só com o cliente X11 compilado, como apontou o CodeRabbit;
51/51 de novo depois da mudança).
