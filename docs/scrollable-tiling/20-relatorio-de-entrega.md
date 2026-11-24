# Relatório de entrega

- Versão anterior: 0.4.1. Nova: **0.5.0** (tag `v0.5.0`).
- PRs: big-windowmaker/wmake-zones#15 (merge), packages (wmake-zones
  0.5.0, wlmaker -10 com 0182–0184), big-wm (atalhos do Wayland e
  dependências), este (capturas e o registro da VM).
- Testes: 10 suítes, 267 verificações locais (Wayland com o wlmaker real),
  0 falhas, 0 puladas; CI do wmake-zones verde (sem wlmaker no contêiner:
  as duas suítes do Wayland pulam lá, rodaram aqui e na VM); wlmaker ctest
  30/30; ASan + UBSan limpos depois de corrigir um uso depois de liberar
  na saída do wlmaker.
- Desempenho: repouso sem acordadas, +40 a +60 KiB de PSS; ligar o
  mosaico alguns ms a mais uma vez por troca de modo (abaixo de 16 ms com
  50 janelas). Detalhes em [12](12-performance.md).
- Bugs corrigidos: ver [18](18-bugs-e-correcoes.md) (oito antes da revisão,
  nove apontados pelo CodeRabbit e corrigidos com testes).
- Limitações: ver [19](19-validacao-final.md).
