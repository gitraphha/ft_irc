/* Preloaded into the server under test: logs every close() and every failed
 * accept()/send()/recv() with its errno, so a second close of the same fd
 * (EBADF) or an unexpected -1 is visible. Linux: LD_PRELOAD; macOS: dyld interpose. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void note(const char *name, int fd, long r, int e) {
    if (r < 0)
        fprintf(stderr, "[sys] %s(%d) -> -1 %s\n", name, fd, strerror(e));
    else if (name[0] == 'c')
        fprintf(stderr, "[sys] %s(%d) -> %ld\n", name, fd, r);
}

#ifdef __APPLE__
#define INTERPOSE(mine, orig) \
    __attribute__((used)) static struct { const void *m; const void *o; } \
    _ip_##orig __attribute__((section("__DATA,__interpose"))) = \
        {(const void *)(unsigned long)&mine, (const void *)(unsigned long)&orig};
int my_close(int fd) { int r = close(fd); int e = errno; note("close", fd, r, e); errno = e; return r; }
int my_accept(int fd, struct sockaddr *a, socklen_t *l) { int r = accept(fd, a, l); int e = errno; note("accept", fd, r, e); errno = e; return r; }
ssize_t my_send(int fd, const void *b, size_t n, int f) { ssize_t r = send(fd, b, n, f); int e = errno; note("send", fd, r, e); errno = e; return r; }
ssize_t my_recv(int fd, void *b, size_t n, int f) { ssize_t r = recv(fd, b, n, f); int e = errno; note("recv", fd, r, e); errno = e; return r; }
INTERPOSE(my_close, close)
INTERPOSE(my_accept, accept)
INTERPOSE(my_send, send)
INTERPOSE(my_recv, recv)
#else
int close(int fd) {
    static int (*real)(int) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "close");
    int r = real(fd); int e = errno; note("close", fd, r, e); errno = e; return r;
}
int accept(int fd, struct sockaddr *a, socklen_t *l) {
    static int (*real)(int, struct sockaddr *, socklen_t *) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "accept");
    int r = real(fd, a, l); int e = errno; note("accept", fd, r, e); errno = e; return r;
}
ssize_t send(int fd, const void *b, size_t n, int f) {
    static ssize_t (*real)(int, const void *, size_t, int) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "send");
    ssize_t r = real(fd, b, n, f); int e = errno; note("send", fd, r, e); errno = e; return r;
}
ssize_t recv(int fd, void *b, size_t n, int f) {
    static ssize_t (*real)(int, void *, size_t, int) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "recv");
    ssize_t r = real(fd, b, n, f); int e = errno; note("recv", fd, r, e); errno = e; return r;
}
#endif
