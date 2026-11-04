/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IPC local do wmake-zones: socket Unix em $XDG_RUNTIME_DIR/wmake-zones/
 * (diretório 0700), uma conexão por comando, só o próprio usuário.
 *
 *   x11-<display>.sock        backend X11 (daemon wmake-zones)
 *   wayland-<display>.sock    wlmaker (o compositor serve o mesmo protocolo)
 */
#ifndef WZ_IPC_H
#define WZ_IPC_H

#include <stddef.h>

#define WZ_IPC_MAX 2048

/* backend: "x11" ou "wayland". NULL se o display não está definido. */
char *wz_ipc_path(const char *backend);
/* Caminho do socket da sessão atual (Wayland se houver, senão X11). */
char *wz_ipc_session_path(void);

int wz_ipc_server(const char *path);  /* fd ouvindo, ou -1 */
int wz_ipc_peer_ok(int fd);           /* mesmo uid? */

/* Manda um comando e lê a resposta (até a linha vazia). 0 = ok, -1 sem
 * daemon. */
int wz_ipc_request(const char *path, const char *cmd, char *out, size_t outlen);

/* Registro curto em memória, para diagnóstico ("wmake-zones log"). */
void wz_log_line(const char *line);
const char *wz_log_text(void);

#endif
