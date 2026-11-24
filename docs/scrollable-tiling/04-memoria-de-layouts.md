# Memória por modo

| Modo | O que é guardado | Onde |
|---|---|---|
| Zonas / desligado (livre) | posição, tamanho, zona, maximização | X11: `_WMAKE_ZONES_FREE` na janela; Wayland: `zones_window_t` |
| Mosaico | ordem, árvore BSP, proporção da principal | conjuntos por monitor e área de trabalho (já existia) |
| Rolagem | colunas, larguras, alturas, ordem, coluna e linha ativas, primeira coluna visível | X11: `_WMAKE_ZONES_SCROLL` na raiz; Wayland: `ssets[]` |

* A memória vale para a sessão e sobrevive a um reinício do serviço X11
  (propriedades X). Entre sessões (logout) não: identificar a mesma janela
  depois de reabrir o aplicativo não é seguro (ver pendências em
  [19](19-validacao-final.md)).
* A janela que fecha some das faixas na rodada seguinte; uma nova ganha
  coluna sem desarrumar as outras.
* Monitores trocados: as zonas se refazem como antes; a faixa de um
  monitor que saiu se junta à do monitor onde as janelas estão.
* Verificado: `x11-scroll` e `wayland-scroll` comparam a geometria exata de
  cada janela em Z → M → Z, R → Z, R → desligado, e a faixa em Z → R.
