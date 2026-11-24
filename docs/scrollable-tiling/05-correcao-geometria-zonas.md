# Arrastar mantém o tamanho

Causa: soltar uma janela encaixada fora das zonas (no fluxo padrão,
qualquer lugar fora das miniaturas do painel) devolvia o tamanho de antes
do encaixe (`RestoreSizeOnUnsnap=true`, documentado), perdendo o ajuste
feito à mão.

Correção (núcleo, X11 e wlmaker):

* `RestoreSizeOnUnsnap` passa a `false`: a janela fica com o tamanho que
  tem e sai da zona. A geometria de antes do encaixe continua guardada para
  o `restore` (Super+Ctrl+0).
* A opção continua nas Preferências para quem preferir o comportamento
  antigo.
* O início do arrasto nunca mudou o tamanho; a mudança era ao soltar.

Testes: `x11-scroll` (encaixar, redimensionar à mão, arrastar e soltar
fora: tamanho igual e zona limpa); `wayland-functional`.
