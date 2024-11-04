#define _POSIX_C_SOURCE 200809
#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "signum.h"

int
main(int argc, char *argv[])
{
  if (argc < 2) errx(1, "Not enough arguments");
  else if (argc > 2) errx(1, "Too many arguments");

  int sig = signum(argv[1]);
  if (sig == -1) errx(1, "%s", argv[1]);
  switch (sig) {
    case SIGCHLD:
    case SIGCONT:
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
    case SIGURG:
      errx(1, "%s: Not a terminating signal", argv[1]);
  }

  if (sig != SIGKILL) {
    if (signal(sig, SIG_DFL) == SIG_ERR) err(1, "signal()");
  }
  
  if (raise(sig)) err(1, "raise()");

  /* Wait for termianting signal */
  sigset_t s;
  sigemptyset(&s);
  sigsuspend(&s);
  err(1, "sigsupend()");
  return 0;
}
