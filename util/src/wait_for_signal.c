#define _POSIX_C_SOURCE 200809
#include <err.h>
#include <signal.h>

#include "signum.h"

int
main(int argc, char *argv[])
{
  sigset_t s;
  sigfillset(&s);
  for (int i = 1; i < argc; ++i) {
    int signo = signum(argv[i]);
    if (signo < 0) err(1, "%s", argv[i]);
    sigdelset(&s, signo);
  }
  sigsuspend(&s);
}
