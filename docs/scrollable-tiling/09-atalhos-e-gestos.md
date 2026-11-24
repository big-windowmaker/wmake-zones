# Atalhos e gestos

Auditoria: nenhum dos atalhos novos é usado pelo Big WindowMaker no Window
Maker nem no wlmaker (`big-wm` main de 24/11).

| Atalho | Ação | X11 | Wayland |
|---|---|---|---|
| Super+Shift+R | Rolagem | serviço | `Config.plist` do big-wm |
| Super+Ctrl+Home / End | primeira / última coluna | serviço | `Config.plist` |
| Super+Ctrl+C | centralizar | serviço | `Config.plist` |
| Super+Ctrl+[ / ] | juntar / separar | serviço | `Config.plist` |
| Super+Alt+setas, Super+Shift+setas, Super+Ctrl+= / − | na faixa: foco, mover, largura | já existiam | já existiam |
| Ctrl+Alt + botão | mover / redimensionar | serviço | toolkit |

Super+Shift+Z continua ligando/desligando o mosaico; Mudar para Zonas e
para Mosaico existem como ações sem tecla (configuráveis). No Wayland, o
`big-wm-firstrun` acrescenta as teclas novas a um `Config.plist` existente
só onde a tecla está livre.

Gestos de touchpad: não implementados (o X11 do Window Maker não tem
gestos; no wlmaker exigiria o protocolo de gestos no toolkit). Teclado,
roda do dockapp e arrasto cobrem a navegação.
