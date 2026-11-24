# Comandos

Mesmo protocolo (uma linha por conexão, socket do usuário) nos dois
backends; os comandos antigos não mudaram.

```
wmake-zones mode zones|tiling|scroll|off|toggle|enable
wmake-zones scroll left|right|up|down|first|last|center|view-left|view-right
wmake-zones column next|previous|move-left|move-right|merge|split|width N%|+|-
wmake-zones status        # scroll=on|off, strip=..., widths=..., scrolled=, parked=
```

* `toggle` passa a ir de Zonas a Mosaico a Rolagem.
* `mode` que liga algo com o serviço X11 parado grava o modo e sobe o
  serviço (antes só `zones on`/`autotile on`).
* `focus`, `swap`/`move`, `grow`/`shrink` agem na faixa quando a janela
  ativa está nela.
* Erros: `error=a Rolagem está desligada` e `error=a janela ativa não está
  na faixa`, com código de saída 1.
* Um dockapp antigo continua funcionando com o compositor novo (os
  comandos antigos não mudaram); o compositor ignora chaves que não
  conhece.
