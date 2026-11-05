# Configuração

Arquivo: `$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf` (INI compatível com
GKeyFile). Tudo também se ajusta nas preferências; o exemplo comentado
completo fica em `/usr/share/doc/wmake-zones/wmake-zones.conf.example`.

| Seção | Chave | Padrão | |
|---|---|---|---|
| General | `Zones` | true | zonas ao arrastar (desde a 0.3; `false` gravado vale) |
| | `Autotile` | false | mosaico automático |
| | `LastMode` | zones | modo religado pelo dockapp (`zones`, `tiling`) |
| | `DragFlow` | layouts | `layouts` (painel primeiro), `zones`, `layouts-modifier`, `layouts-edge` |
| | `LayoutDwell` | 250 | ms parado sobre uma miniatura até mostrar as zonas dela |
| | `DragLayouts` | halves,quarters,thirds,main-stack,two-top,grid-2x3,ultrawide | miniaturas do painel; os layouts do usuário entram depois |
| | `HistoryDepth` | 20 | passos do desfazer (1–100) |
| | `FocusEdge` | cross | foco na borda do monitor: `cross`, `stop`, `wrap` |
| | `Activation` | drag | `drag`, `modifier` ou `edge` |
| | `Modifier` | super | `super`, `alt`, `ctrl`, `shift` |
| | `ShowDelay` | 120 | ms até mostrar as zonas |
| | `MinDrag` | 24 | px de arrasto antes de considerar |
| | `Edge` | 48 | px da borda (ativação `edge`) |
| | `Span` | 24 | px perto da fronteira para unir duas zonas |
| | `ShowNumbers` | true | |
| | `RestoreSizeOnUnsnap` | true | tamanho de antes ao arrastar para fora |
| | `AvoidDock` | true | não cobrir a coluna do Dock/Clip |
| | `Rules` | true | aplicar as regras |
| | `KeepRunning` | false | serviço ativo com tudo desligado (atalhos) |
| | `Layout` | (automático) | layout geral; sem ele, pelo formato do monitor (`ultrawide` a partir de 21:9, `portrait` em pé, `halves`) |
| | `Gap`, `OuterGap` | 6, 6 | espaçamento das zonas |
| Tiling | `Algorithm` | master | ver [mosaico](auto-tiling.md) |
| | `MasterRatio` | 0.6 | 0.1–0.9 |
| | `Masters` | 1 | |
| | `MasterPosition` | left | `left`, `right`, `top`, `bottom` |
| | `Gap`, `OuterGap` | 6, 6 | |
| | `SmartGaps` | true | |
| Dockapp | `LeftClick` | mode | `mode` (Zonas ↔ Mosaico), `enable`, `layouts`, `zones`, `autotile`, `none` |
| | `MiddleClick` | enable | desliga / religa o último modo |
| | `Wheel` | layout | `layout` (anterior/próximo) ou `none` |
| Appearance | `Highlight` | auto | `#rrggbb` ou a cor da barra de título do tema |
| | `Opacity` | 35 | 0–100 |
| Keys | `Picker`, `MoveLeft`, `Undo`, `Redo`... | ver README | vazio desliga |
| Layout *id* | `Tree`, `Name` | | layout do usuário |
| Monitor *nome* [Workspace *n*] / Workspace *n* | `Layout`, `Autotile`, `Algorithm` | | |
| Rule *id* | `Class`, `Instance`, `Title`, `Action`, `Zone`, `Layout`, `Monitor`, `Workspace` | | ver abaixo |
| State | `RestoreWindowSnapping` | | uso interno |

Valores inválidos ficam no padrão; um layout com árvore inválida é
ignorado. O serviço só escreve o arquivo por comandos do usuário (ligar,
trocar layout), nunca durante um arrasto, e preserva comentários e chaves
desconhecidas (escrita atômica, `rename`). Cada gravação trava um
`wmake-zones.conf.lock` ao lado, para o serviço e as preferências não
apagarem as mudanças um do outro; se o arquivo for um link simbólico
(dotfiles), o alvo é que muda. Espaços nas pontas, tab e quebras de linha
são gravados como `\s`, `\t`, `\r` e `\n`, como no GKeyFile. Comentários
ficam em linhas próprias (`#` ou `;`), nunca depois de um valor.

## Regras por aplicativo

```ini
[Rule navegador]
# app_id no Wayland; classe do WM_CLASS no X11 e no XWayland
Class=firefox
# float, tile, zone, ignore ou none
Action=zone
Zone=2
# Opcionais: a zona deste layout, o monitor e a área de trabalho
Layout=thirds
Monitor=DP-1
Workspace=2
```

`Class` e `Instance` comparam sem diferenciar maiúsculas; `Title` é um
trecho do título (nunca a única pista, se houver classe). No Wayland a
classe é o `app_id` do cliente (o que `wmake-zones windows -l` mostra);
pelo XWayland, a classe e a instância do WM_CLASS, como no X11. A primeira
regra que casar vale, uma vez, quando a janela aparece. Diálogos (com
janela-mãe, modais) nunca entram no mosaico nem são levados pelas regras;
`ignore` tira a janela também das zonas.

## Comandos

`wmake-zones <comando>` fala com o serviço da sessão (X11 ou wlmaker) pelo
socket em `$XDG_RUNTIME_DIR/wmake-zones/` (diretório 0700; o serviço confere o
uid do outro lado). Só comandos de uma lista fixa; nada do cliente vira
execução de programa.

`status`, `mode zones|tiling|off|toggle|enable`, `zones on|off|toggle`,
`autotile on|off|toggle`, `layout ID|next|prev`, `layouts`,
`algorithm NOME`, `picker`, `arrange`, `snap N`,
`move|swap|focus left|right|up|down`, `restore`, `restore-all`, `float`,
`promote`, `grow`, `shrink`, `undo`, `redo`, `preview ÁRVORE`,
`windows [-l]`, `overlay`, `editor`, `prefs [PÁGINA]`, `reload`, `log`,
`ping`, `quit`; `help` mostra a lista (sem falar com o serviço) e
`man wmake-zones` descreve todos. Os mesmos comandos valem no X11 e no
wlmaker.

`mode toggle` é o clique esquerdo do dockapp (Zonas ↔ Mosaico; desligado,
liga o último modo) e `mode enable` o do meio (desliga ou religa o último).

`windows` dá classe, instância e título (o que as regras usam); `windows -l`
dá o monitor, a zona, se está no mosaico e o quadro (`LxA+X+Y`, com a
decoração), para conferir um encaixe:

```
DisplayPort-1	zone=0	tiled=0	846x1068+6886+6	codium	VSCodium
```

## Migração da 0.2

Nada é reescrito: quem nunca mexeu nas zonas passa a tê-las ligadas (a
chave `Zones` não estava gravada); quem as desligou tem `Zones=false` e
continua assim; o mosaico, os layouts, as regras e os atalhos ficam como
estavam. Sem `Layout` gravado, o layout passa a vir do formato do
monitor. No Big WindowMaker, o ladrilho entra no Dock uma vez (o
firstrun marca isso em `~/.config/big-wm`); tirado do Dock, não volta.

Ligar algo com o serviço parado (X11) grava o arquivo e sobe o serviço.
