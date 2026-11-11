# Correções sobre o PR #9

Em commits por cima dos de @deleterium, na mesma branch.

1. **Contorno das miniaturas no destino errado** (`draw_mini`): desenhado
   no `face` do dockapp; agora no `Drawable d` recebido.
2. **Área clicável diferente da desenhada** (Z e M): Z e M dividem a tela
   LCD (x 14..29 e 30..45, com o chanfro); `led_key()` desenha a partir de
   `row_btn`, a mesma tabela do `row_hit()`.
3. **Teclas na borda**: a borda de 3 px do ladrilho de 60 cobria pixels das
   teclas; o clique numa tecla é dela (`row_hit` antes da borda).
4. **Amarelo sem X Shape**: o pixmap é preenchido com a cor do ladrilho do
   tema; sem a extensão, a janela limpa para o fundo do pai (já era
   `ParentRelative`) e só as partes desenhadas são copiadas. A máscara é
   aplicada uma vez por janela, não a cada redesenho. `WZX_NO_SHAPE=1`
   simula o servidor sem a extensão (testes).
5. **Margens da tela**: 2 px nos quatro lados (eram 2 à esquerda e em cima,
   4 à direita e embaixo).
6. Comentários: cabeçalho (60x60, o que é desenhado), `wzx_draw_bevel_1px`,
   constantes de cor em minúsculas.

## Testes

- `tests/x11-dockapp.sh`: nova paleta; modos pelos LEDs e pelo ciano meio
  aceso; nunca amarelo; todas as bordas das teclas; o quadro achado pela
  janela do dockapp (o Window Maker move o ícone solto 2 px depois de
  abrir).
- `x11-dockapp-noshape` no meson.
- `wayland-functional` no meson (suíte `wayland`; SKIP sem um wlmaker com
  as zonas).
- Capturas `docs/screenshots/dockapp.png` e `dockapp-algoritmos.png`
  refeitas com o visual novo; `docs/testing.md` atualizado.
