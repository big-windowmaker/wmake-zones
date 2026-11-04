# Configuração

Arquivo: `$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf` (INI compatível com
GKeyFile). Tudo também se ajusta nas preferências; o exemplo comentado
completo fica em `/usr/share/doc/wmake-zones/wmake-zones.conf.example`.

| Seção | Chave | Padrão | |
|---|---|---|---|
| General | `Zones` | false | zonas ao arrastar |
| | `Autotile` | false | mosaico automático |
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
| | `Layout` | halves | layout geral |
| | `Gap`, `OuterGap` | 6, 6 | espaçamento das zonas |
| Tiling | `Algorithm` | master | ver [mosaico](auto-tiling.md) |
| | `MasterRatio` | 0.6 | 0.1–0.9 |
| | `Masters` | 1 | |
| | `MasterPosition` | left | `left`, `right`, `top`, `bottom` |
| | `Gap`, `OuterGap` | 6, 6 | |
| | `SmartGaps` | true | |
| Appearance | `Highlight` | auto | `#rrggbb` ou a cor da barra de título do tema |
| | `Opacity` | 35 | 0–100 |
| Keys | `Picker`, `MoveLeft`... | ver README | vazio desliga |
| Layout *id* | `Tree`, `Name` | | layout do usuário |
| Monitor *nome* [Workspace *n*] / Workspace *n* | `Layout`, `Autotile`, `Algorithm` | | |
| Rule *id* | `Class`, `Instance`, `Title`, `Action`, `Zone`, `Layout`, `Monitor` | | |
| State | `RestoreWindowSnapping` | | uso interno |

Valores inválidos ficam no padrão; um layout com árvore inválida é
ignorado. O serviço só escreve o arquivo por comandos do usuário (ligar,
trocar layout), nunca durante um arrasto, e preserva comentários e chaves
desconhecidas (escrita atômica, `rename`).

## Comandos

`wmake-zones <comando>` fala com o serviço da sessão (X11 ou wlmaker) pelo
socket em `$XDG_RUNTIME_DIR/wmake-zones/` (diretório 0700; o serviço confere o
uid do outro lado). Só comandos de uma lista fixa; nada do cliente vira
execução de programa.

`status`, `zones on|off|toggle`, `autotile on|off|toggle`,
`layout ID|next|prev`, `layouts`, `algorithm NOME`, `picker`, `arrange`,
`snap N`, `move|swap|focus left|right|up|down`, `restore`, `restore-all`,
`float`, `promote`, `grow`, `shrink`, `undo`, `preview ÁRVORE`, `windows`,
`editor`, `prefs`, `reload`, `log`, `quit`.

Ligar algo com o serviço parado (X11) grava o arquivo e sobe o serviço.
