# Testes na VM (Virt-Manager)

VM `bwm-clip-grade` (QEMU/KVM, Big WindowMaker instalado pela ISO, 1280×800,
picom no X11), snapshot `antes-rolagem-0.5.0` antes de instalar
`wmake-zones` 0.5.0-1 e `wlmaker` -10 (compilados da branch). Usuário de
teste, login pelo greeter (wdm) com o teclado da VM; entrada real do mouse
por um dispositivo uinput (`uptr`), que passa pelo kernel e pelo libinput
como um mouse de verdade.

| Sessão | Verificação | Resultado |
|---|---|---|
| X11 | Thunar (encaixado na zona 1), AshyTerm, xterm, Calculadora (GTK4): Z → M → R → Z | 3 de 4 voltaram idênticas; a Calculadora voltou mais alta porque o GTK4 subiu o próprio mínimo para 616 px depois de ficar alta na faixa (o mínimo do aplicativo vence, como nas Zonas) |
| X11 | Rolagem: colunas inteiras, as outras em x = 1536 (fora da tela) | verificado (captura) |
| X11 | Dockapp: M e R acesos, colunas e traços das escondidas | verificado (captura ampliada) |
| X11 | Ctrl+Alt+arrastar com xdotool e com entrada real | verificado: move a janela sob o ponteiro (num teste ela estava coberta por outra, que foi a que se moveu: correto) |
| Wayland | As mesmas janelas (xterm pelo XWayland): Z → M → R → Z | as 4 voltaram idênticas |
| Wayland | Rolagem: duas colunas, duas estacionadas | verificado (captura) |
| Wayland | Dockapp do wlmaker: Z, M e R | verificado (captura ampliada) |
| Wayland | Ctrl+Alt + esquerdo (mover) e + direito (redimensionar pelo canto), entrada real | verificado: 788,0 → 588,40; 362×649 → 452×679 |
| Wayland | Preferências em pt_BR, página Rolagem | verificado (captura) |
| Ambas | Atualização da 0.4.1 com o arquivo de configuração existente | verificado (o serviço e o wlmaker leram o arquivo antigo; `RestoreSizeOnUnsnap` só fica `true` para quem o gravou) |

Não deu para simular na VM: dois monitores no Wayland (o X11 com dois
monitores está no teste com Xephyr), HiDPI, touchpad.
