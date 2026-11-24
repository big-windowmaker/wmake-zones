# Testes automatizados

`meson test -C build` (com `WLMAKER=` apontando para o wlmaker com os
patches novos): 10 suítes, 267 verificações, todas aprovadas, nenhuma
pulada. Detalhes por suíte em [docs/testing.md](../testing.md#rolagem-e-memória-por-modo-05).

Também: wlmaker `ctest` (30/30), ASan + UBSan no wmake-zones e no
wlmaker (núcleo, X11 e Wayland), `msgfmt --check` dos catálogos.
