#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <err.h>

#include "signum.h"

typedef void (*sighandler_t)(int);

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    int signo = signum(argv[i]);
    if (signo < 0) err(1, "%s", argv[i]);
    sighandler_t sh = signal(signo, SIG_DFL);
    char const *s = "Unknown Disposition";
#define MAP(x) if (sh == x) s = #x;
    MAP(SIG_DFL);
    MAP(SIG_ERR);
    MAP(SIG_IGN);

    printf("%s: %s\n", argv[i], s);
  }
}
