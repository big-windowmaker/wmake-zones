# Bugs achados e corrigidos

| Bug | Onde | Correção |
|---|---|---|
| Z → M → Z tirava a janela da zona e a devolvia ao tamanho de antes do encaixe | X11 e wlmaker | memória do modo livre |
| Arrastar uma janela encaixada para fora perdia o tamanho ajustado | X11 e wlmaker | `RestoreSizeOnUnsnap=false` |
| Com o tamanho mantido, a janela solta fora das zonas continuava marcada na zona | X11 e wlmaker | a zona é limpa |
| Redimensionar pelo botão direito não terminava ao soltar | toolkit do wlmaker | o workspace também encerra no botão direito |
| Uso depois de liberar ao fechar o wlmaker (ouvintes de ativação) | zones.c (código novo) | desligados ao destruir o módulo |
| `mode tiling/scroll` com o serviço X11 parado não fazia nada | cliente `wmake-zones` | grava e sobe o serviço |
| Atalho Refazer sem rótulo nas Preferências | wzp-main.c | tabela com índices explícitos |
| Traduções fuzzy erradas do msgmerge ("Previous column" → "Três colunas") | pt_BR.po | revisadas |
| Separar da primeira coluna para a posição 0 ia para o fim da faixa | wz-scroll | coluna criada direto na posição |
| Separar no limite de 256 colunas perdia a janela | wz-scroll | espaço reservado antes de tirar a janela |
| `view-right` não chegava à última coluna com uma larga no fim | wz-scroll | preenchimento pela esquerda só no fim da faixa |
| Gaps enormes davam altura negativa | wz-scroll | o gap encolhe antes das janelas |
| Troca de modo gravava quatro chaves em quatro trocas do arquivo | wz-config | `wz_ini_set_many`, uma troca só |
| Dockapp lia 64 larguras de uma faixa de até 256 | dockapps | vetor do tamanho do limite |
| Sair da Rolagem com o mosaico em um monitor só deixava janelas estacionadas nos outros | X11 e wlmaker | decisão por janela |
| Faixas vazias nunca liberavam o lugar | X11 e wlmaker | liberadas depois do sync |
| `mode toggle` com o serviço parado resolvia duas vezes | cliente | grava o modo resolvido |
| Ctrl+Alt engolia o clique com um Window Maker sem `_NET_WM_MOVERESIZE` | X11 | só pega o clique com o suporte anunciado |
