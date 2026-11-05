/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "wz-ipc.h"
#include "wz-config.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void
sanitize(char *s)
{
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' ||
              *s == '_' || *s == '.'))
            *s = '_';
}

char *
wz_ipc_path(const char *backend)
{
    const char *disp = !strcmp(backend, "wayland") ? getenv("WAYLAND_DISPLAY") : getenv("DISPLAY");
    if (!disp || !*disp)
        return NULL;
    char d[128];
    if (!strcmp(backend, "x11")) {
        /* ":0.0" e "localhost:0" -> "0" */
        const char *colon = strrchr(disp, ':');
        snprintf(d, sizeof d, "%s", colon ? colon + 1 : disp);
        char *dot = strchr(d, '.');
        if (dot)
            *dot = 0;
    } else {
        const char *slash = strrchr(disp, '/');
        snprintf(d, sizeof d, "%s", slash ? slash + 1 : disp);
    }
    sanitize(d);
    char *dir = wz_runtime_dir();
    char *p = NULL;
    if (asprintf(&p, "%s/%s-%s.sock", dir, backend, d) < 0)
        p = NULL;
    free(dir);
    return p;
}

char *
wz_ipc_session_path(void)
{
    const char *t = getenv("XDG_SESSION_TYPE");
    int wayland = getenv("WAYLAND_DISPLAY") && (!t || strcmp(t, "x11") != 0);
    char *p = wz_ipc_path(wayland ? "wayland" : "x11");
    if (!p)
        p = wz_ipc_path(wayland ? "x11" : "wayland");
    return p;
}

int
wz_ipc_peer_ok(int fd)
{
    struct ucred cr;
    socklen_t l = sizeof cr;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &l) != 0)
        return 0;
    return cr.uid == getuid();
}

int
wz_ipc_server(const char *path)
{
    char dir[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (strlen(path) >= sizeof dir)
        return -1;
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return -1;
    *slash = 0;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST)
        return -1;
    struct stat st;
    /* O diretório precisa ser nosso e fechado para os outros. */
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid())
        return -1;
    if ((st.st_mode & 077) != 0)
        chmod(dir, 0700);

    /* Já tem alguém respondendo? Então não é nosso. */
    char out[64];
    if (wz_ipc_request(path, "ping", out, sizeof out) == 0)
        return -2;
    unlink(path); /* sobra de um processo que morreu */

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    mode_t old = umask(077);
    int r = bind(fd, (struct sockaddr *)&a, sizeof a);
    umask(old);
    if (r != 0 || listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int
wz_ipc_request(const char *path, const char *cmd, char *out, size_t outlen)
{
    if (out && outlen)
        out[0] = 0;
    if (!path)
        return -1;
    /* Sem bloquear: com a fila do servidor cheia o connect falha na hora
     * em vez de prender quem pediu. */
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    /* Do outro lado precisa estar o nosso usuário (no fallback em /tmp,
     * outro usuário poderia ter criado o socket antes). */
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0 || !wz_ipc_peer_ok(fd)) {
        close(fd);
        return -1;
    }
    char line[WZ_IPC_MAX];
    snprintf(line, sizeof line, "%s\n", cmd);
    if (send(fd, line, strlen(line), MSG_NOSIGNAL) < 0) {
        close(fd);
        return -1;
    }
    size_t got = 0, total = 0;
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 3000) <= 0)
            break;
        char tmp[4096];
        ssize_t n = recv(fd, tmp, sizeof tmp, 0);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n <= 0)
            break;
        total += (size_t)n;
        if (out && got < outlen - 1) {
            size_t c = (size_t)n < outlen - 1 - got ? (size_t)n : outlen - 1 - got;
            memcpy(out + got, tmp, c);
            got += c;
            out[got] = 0;
        }
    }
    close(fd);
    /* Nenhuma resposta: o servidor travou ou não é um wmake-zones. */
    if (total == 0)
        return -1;
    /* Tira a linha vazia final. */
    if (out && got >= 2 && out[got - 1] == '\n' && out[got - 2] == '\n')
        out[got - 1] = 0;
    return 0;
}

/* Lê um pedido (até o fim da linha) de um cliente já aceito, sem esperar
 * mais que alguns milissegundos por um cliente mudo. */
int
wz_ipc_read_request(int fd, char *line, size_t len)
{
    size_t got = 0;
    int waited = 0;
    if (!len)
        return -1;
    line[0] = 0;
    while (got < len - 1) {
        ssize_t n = recv(fd, line + got, len - 1 - got, MSG_DONTWAIT);
        if (n > 0) {
            got += (size_t)n;
            line[got] = 0;
            if (memchr(line, '\n', got))
                break;
            continue;
        }
        if (n == 0)
            break;
        if (errno != EAGAIN && errno != EINTR)
            return -1;
        if (waited >= 100)
            break;
        struct pollfd p = { fd, POLLIN, 0 };
        poll(&p, 1, 20);
        waited += 20;
    }
    return got ? 0 : -1;
}

/* Apaga o socket só se ainda for o que este processo criou (outra instância
 * pode já ter posto o dela no mesmo caminho). */
void
wz_ipc_unlink(const char *path, const struct stat *mine)
{
    struct stat st;
    if (path && mine && stat(path, &st) == 0 && st.st_ino == mine->st_ino && st.st_dev == mine->st_dev)
        unlink(path);
}

int
wz_ipc_request_session(const char *cmd, char *out, size_t outlen)
{
    const char *t = getenv("XDG_SESSION_TYPE");
    int wayland = getenv("WAYLAND_DISPLAY") && (!t || strcmp(t, "x11") != 0);
    const char *order[2] = { wayland ? "wayland" : "x11", wayland ? "x11" : "wayland" };
    for (int i = 0; i < 2; i++) {
        char *p = wz_ipc_path(order[i]);
        int r = p ? wz_ipc_request(p, cmd, out, outlen) : -1;
        free(p);
        if (r == 0)
            return 0;
    }
    return -1;
}

/* ---- registro ----------------------------------------------------------- */

#define LOG_LINES 48
static char log_ring[LOG_LINES][200];
static int log_next, log_count;
static char log_buf[LOG_LINES * 201 + 1];

void
wz_log_line(const char *line)
{
    snprintf(log_ring[log_next], sizeof log_ring[0], "%s", line);
    log_next = (log_next + 1) % LOG_LINES;
    if (log_count < LOG_LINES)
        log_count++;
}

const char *
wz_log_text(void)
{
    log_buf[0] = 0;
    size_t used = 0;
    for (int i = 0; i < log_count; i++) {
        int k = (log_next - log_count + i + LOG_LINES) % LOG_LINES;
        int n = snprintf(log_buf + used, sizeof log_buf - used, "%s\n", log_ring[k]);
        if (n < 0 || (size_t)n >= sizeof log_buf - used)
            break;
        used += (size_t)n;
    }
    return log_buf;
}
