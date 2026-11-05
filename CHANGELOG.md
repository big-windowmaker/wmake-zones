# Mudanças

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
