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
