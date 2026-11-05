# Mudanças

## 0.3.0 — 2026-11-05

Zonas ligadas por padrão, primeiro o layout e depois a zona, o Wayland com
tudo o que o X11 tem e um dockapp que alterna Zonas e Mosaico num clique.

### Primeiro o layout, depois a zona

- Ao arrastar uma janela aparece um painel com as miniaturas dos layouts,
  no estilo dos menus do Window Maker. Parar o ponteiro sobre uma
  miniatura (250 ms, `LayoutDwell`) mostra as zonas daquele layout no
  monitor; soltar numa zona de uma miniatura encaixa direto, sem esperar.
  Tudo no mesmo arrasto: o painel não pega o ponteiro.
- O painel fica longe do ponteiro (embaixo se ele está no terço de cima),
  só começa a valer depois que o ponteiro sai de cima dele e se ajusta à
  fonte do tema (Xft.dpi). Soltar no painel fora das miniaturas deixa a
  janela onde foi solta; Esc cancela.
- O layout escolhido vira o do monitor, no mesmo passo do desfazer que o
  encaixe.
- `DragFlow`: `layouts` (padrão), `zones` (as zonas do layout atual, como
  antes), `layouts-modifier` (o painel só com o modificador) e
  `layouts-edge` (o painel na borda de cima). `DragLayouts` escolhe as
  miniaturas; os layouts do usuário entram sempre.

### Padrões novos

- Zonas ligadas por padrão; o mosaico continua desligado (nenhuma janela
  muda de lugar sozinha). Quem desligou as zonas tem `Zones=false` gravado
  e continua assim; layouts e configurações existentes não mudam.
- Sem layout escolhido, o padrão vem do formato do monitor: `ultrawide` a
  partir de 21:9, `portrait` num monitor em pé, metades nos outros. As
  preferências oferecem "Automático".

### Dockapp

- Clique esquerdo alterna Zonas e Mosaico; o do meio desliga e religa o
  último modo; a roda troca o layout; o direito abre o menu. Tudo
  configurável em `[Dockapp]` e nas preferências.
- O ladrilho mostra a miniatura do layout e duas teclas com LED: Z (zonas,
  verde) e M (mosaico, âmbar); nenhuma acesa, desligado. Um balão discreto
  diz o modo e o layout a cada troca (X11); no Wayland a roda mostra uma
  prévia breve das zonas.
- Menu no estilo do Window Maker, com submenus: modo, layout, algoritmo do
  mosaico, desfazer, refazer, restaurar, organizar, flutuante/mosaico,
  configurações (editor, preferências, atalhos, regras, monitores), ajuda
  e "Tudo flutuante agora", a saída de emergência.
- No Big WindowMaker o ladrilho já vem no Dock, na coluna da direita.

### Wayland igual ao X11

- Seletor Super+Z: o teclado inteiro vai para ele (grab no compositor);
  números, setas, Enter, Tab, Esc e clique numa zona.
- Focar a janela ao lado (Super+Alt+setas).
- Desfazer e refazer (Super+Ctrl+Backspace, com Shift para refazer).
- Regras por aplicativo pelo `app_id` (Wayland) ou pela classe e instância
  do WM_CLASS (XWayland), nunca só pelo título: flutuante, mosaico, zona,
  ignorar, e o monitor e a área de trabalho em que a janela abre. Diálogos
  (com janela-mãe, modais) ficam fora do mosaico e das zonas.
- Ativação só com o modificador, BSP com árvore persistente, proporção da
  principal pelo mouse (redimensionar a janela) e células do mosaico ao
  arrastar uma janela do mosaico.
- Um movimento rápido do mouse não termina mais o arrasto de uma janela no
  wlmaker (antes, um evento que tirasse o ponteiro de cima da barra de
  título encerrava o movimento onde ele estava).
- `windows -l` traz o `app_id`; `status` traz a escala de cada saída.

### Histórico, foco e regras (os dois ambientes)

- Histórico no núcleo, com refazer: encaixes, trocas de layout, modo,
  mosaico ligado/desligado, algoritmo, proporção da principal, trocas de
  lugar e flutuar. Profundidade em `HistoryDepth` (20); cada passo guarda
  só a geometria e os estados das janelas tocadas (56 bytes por janela).
  Janelas fechadas são esquecidas; uma geometria de um monitor
  que saiu volta para dentro do mais perto.
- Foco espacial determinístico: a janela mais natural na direção, sem
  saltar para uma alinhada distante quando há uma ao lado; na borda,
  `FocusEdge`: `cross` (segue para o monitor ao lado), `stop` ou `wrap`.
- Regras: `Action=none` (só o monitor ou a área de trabalho), `Monitor` e
  `Workspace` levam a janela ao abrir, `ignore` tira a janela de tudo,
  `tile` vale até para janelas fixas em todas as áreas.

### Monitores

- X11: além de `RRScreenChangeNotify`, as mudanças de CRTC e de saída
  (monitor desligado sem mudar o tamanho da tela, outro principal,
  reorganização) refazem os monitores, uma vez por lote; janelas que
  ficaram fora de todos os monitores voltam para o mais perto.
- X11: uma janela encaixada num monitor que mudou de resolução volta para
  a mesma zona, refeita na geometria nova (antes ficava com o tamanho
  antigo e podia cobrir o Dock).

### Tela de bloqueio

- X11: o seletor e o menu do dockapp fecham sozinhos depois de 30 s
  parados, soltando teclado e ponteiro (antes, esquecidos abertos,
  impediam a tela de bloqueio de pegar o teclado).
- Wayland: com o seletor aberto, a sessão bloqueada recebe as teclas (a
  senha); antes o seletor as consumia (patch 0117 do wlmaker).

### Comandos

- Novos: `mode zones|tiling|off|toggle|enable`, `redo`, `overlay`
  (diagnóstico), `prefs PÁGINA`; `status` mostra a janela ativa.

### Testes e medições

- 931.773 verificações no núcleo (histórico, foco, modos, layout
  automático); 52 no X11 (painel de layouts, desfazer/refazer com o
  layout, seletor parado fecha sozinho); 45 no Wayland (seletor, foco,
  regras pelo app_id, modos, BSP, diálogo, movimento rápido, painel,
  seletor aberto durante o bloqueio).
- Valgrind (Memcheck): núcleo, serviço X11 durante o teste funcional e
  wlmaker com as zonas durante o teste do Wayland, sem nenhum acesso
  inválido nem memória perdida do nosso código.
- Desempenho igual ao da 0.2: 0,77 MiB em repouso, nenhuma acordada,
  mosaico de 50 janelas em 6,6 ms, overlay em 11,4 ms (2,9 ms só com o
  painel), encaixe em 25 µs.

### wlmaker (pacote do Big WindowMaker)

- 0110: a janela guarda o `app_id` ou o WM_CLASS e se é diálogo.
- 0111: a área de trabalho segura o ponteiro durante mover e redimensionar
  e avisa o fim de um redimensionamento.
- 0112: grab de teclado para partes modais do compositor.
- 0113: seletor, foco, desfazer/refazer, regras, painel de layouts.
- 0114-0116: textos em pt_BR, menu do dockapp e estilo dos ícones da
  bandeja.
- 0117: o seletor larga o teclado quando a sessão é bloqueada.

## 0.2.0 — 2026-11-05

Zonas iguais com gap, serviço e editor mais firmes, mouse de canhoto e
documentação completa. Os achados vieram de uma revisão do código, do
analisador estático do gcc, dos sanitizers e do uso numa sessão real do Big
WindowMaker (X11 e Wayland, numa VM) e num computador com três monitores.

### Zonas e mosaico

- Zonas de mesmo peso ficam do mesmo tamanho quando há gap: o espaço é
  dividido depois de descontar os gaps. Antes, a zona do meio perdia um gap a
  mais (terços de 3440 px saíam 1140/1136/1140; agora 1139/1138/1139). Os
  pesos valem para o tamanho da zona.
- Mosaico: uma janela que não cabe na célula sai uma por vez, a que está
  mais longe de caber primeiro, e o layout é refeito. Antes, todas as que não
  cabiam saíam juntas, até as que caberiam depois.
- Mosaico: janelas não ficam presas no mosaico depois de trocar monitores;
  sombrear ou maximizar não muda mais a proporção da principal; mais de 256
  janelas no mesmo monitor não usam mais retângulos sem valor.
- Wayland: as áreas de trabalho são contadas a partir de 1, como no
  `[Workspace N]` (antes `[Workspace 2]` valia na primeira).

### Serviço e comandos

- Arrasto com os botões trocados (mouse de canhoto): o botão 1 lógico vem do
  servidor X. O soltar do botão é sempre tratado, então o arrasto não fica
  mais preso depois de um menu ou do seletor.
- Programas abertos pelo serviço (preferências, editor, Sobre) não herdam
  mais os sinais bloqueados e fecham normalmente.
- O serviço e as preferências não apagam mais as mudanças um do outro (trava
  ao gravar); links simbólicos da configuração são mantidos; espaços nas
  pontas, tab e quebras de linha sobrevivem à gravação.
- Um cliente que conecta e não manda nada não trava mais o serviço; no
  wlmaker o socket é lido pelo laço de eventos e nunca bloqueia o
  compositor. O cliente confere o dono do socket.
- Fim da sessão X11: o socket de controle é apagado. `zones on` com o serviço
  parado sobe o mesmo binário, não o primeiro do `PATH`.
- Troca de layout de teclado (`setxkbmap`) refaz os atalhos.
- Seletor (Super+Z): Tab troca o layout do monitor mostrado; não herda mais a
  prévia do editor.
- Restaurar todas as janelas é um passo só no desfazer.
- Novos: `wmake-zones help` e `wmake-zones windows -l` (monitor, zona,
  mosaico e geometria, nos dois ambientes).

### Editor

- Teclas ignoradas durante o arrasto de uma divisória (desfazer ou excluir
  nesse momento podia usar memória já liberada).
- Divisões limitadas à profundidade que o leitor de layouts aceita: um layout
  salvo não é mais descartado ao carregar.

### Empacotamento e documentação

- Página de manual `wmake-zones(1)` e metadados AppStream (validados no
  `meson test`).
- Mensagens da linha de comando em inglês com tradução pt_BR completa
  (233 mensagens).
- README novo com capturas da sessão real; documentação de testes,
  desempenho e monitores atualizada.
- Testes: 925.058 verificações no núcleo (iguais em -O0, -O2, ASan e UBSan),
  40 no X11 e 15 no Wayland.

### wlmaker (pacote do Big WindowMaker)

- 0107: socket de controle lido pelo laço de eventos; `windows` com as
  colunas do X11.
- 0108: ícones fora das pastas XDG não são mais procurados na árvore de
  compilação (no pacote, um diretório de `/tmp` que qualquer usuário poderia
  criar).
- 0109: áreas de trabalho contadas a partir de 1.
- O menu `RootMenuDebian.plist` instalado não aponta mais para o diretório
  de compilação.

## 0.1.0 — 2026-11-04

Primeira versão: zonas ao arrastar e pelo teclado, mosaico automático com
oito algoritmos, quinze layouts, editor visual, preferências, dockapps para
o X11 e o wlmaker, regras por aplicativo, configuração por monitor e área de
trabalho, e o módulo de zonas dentro do wlmaker.
