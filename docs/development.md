# Desenvolvimento

## Árvore

```
src/engine/    núcleo sem X11/Wayland (libwmake-zones-engine.a, instalado)
src/x11/       serviço do Window Maker e dockapp X11
src/prefs/     preferências e editor (GTK 3)
src/wayland/   dockapp nativo do wlmaker e protocolos
data/          ícone SVG, .desktop, autostart, exemplo de configuração
po/            traduções (pt_BR)
tests/         núcleo, funcionais X11 e Wayland, medições
docs/          esta documentação (nunca lida pelos programas)
```

## Compilar

```sh
meson setup build -Dprefix=/usr
meson compile -C build
```

Opções: `x11`, `prefs`, `wayland_dockapp` (features), `engine_lib`, `nls`,
`tests`. Para sanitizers: `-Db_sanitize=address,undefined -Db_lundef=false`.

## O lado do wlmaker

O código das zonas no compositor está no fork
`big-windowmaker/wlmaker`, branch `big-wm/zones` (sobre os patches do
pacote), e chega ao pacote como os patches `0104` a `0106`. O CMake do wlmaker procura
`wmake-zones-engine` pelo pkg-config; sem ele, `zones.c` vira stubs. Para
compilar contra um núcleo local:

```sh
meson setup b --prefix=$PWD/prefixo && meson install -C b
PKG_CONFIG_PATH=$PWD/prefixo/lib/pkgconfig cmake -S ../wlmaker -B wlm
```

## Depurar

`WZ_DEBUG=1 wmake-zones` registra os tempos do overlay, do encaixe e do
mosaico; `wmake-zones log` mostra as últimas linhas do registro do serviço
e `wmake-zones status` o estado (monitores, áreas úteis, layout efetivo,
janelas, conflitos de atalhos).

## Convenções

C11 (gnu11), sem dependências no núcleo, mensagens do usuário em inglês com
gettext e tradução pt_BR, comentários em português. Nada de polling: tudo
por eventos; timers só enquanto há interação.
