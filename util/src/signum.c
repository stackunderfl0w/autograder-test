#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "signum.h"

struct sigmap {
  int signum;
  char const *signame;
};

#define MAP(key)                                                               \
  {                                                                            \
    (key), (#key)                                                              \
  }

static struct sigmap sigmap[] = {
    MAP(SIGABRT),   MAP(SIGALRM), MAP(SIGBUS),  MAP(SIGCHLD), MAP(SIGCONT),
    MAP(SIGFPE),    MAP(SIGHUP),  MAP(SIGILL),  MAP(SIGINT),  MAP(SIGKILL),
    MAP(SIGPIPE),   MAP(SIGQUIT), MAP(SIGSEGV), MAP(SIGSTOP), MAP(SIGTERM),
    MAP(SIGTSTP),   MAP(SIGTTIN), MAP(SIGTTOU), MAP(SIGUSR1), MAP(SIGUSR2),
    MAP(SIGPOLL),   MAP(SIGPROF), MAP(SIGSYS),  MAP(SIGTRAP), MAP(SIGURG),
    MAP(SIGVTALRM), MAP(SIGXCPU), MAP(SIGXFSZ),
};

int
signum(char const *signame)
{
  if (!signame) goto err;
  char *end = (char *)signame;
  long signum = strtol(signame, &end, 10);
  if (*signame && !*end) return signum;

  for (size_t i = 0; i < sizeof sigmap / sizeof *sigmap; ++i) {
    if (strcmp(signame, sigmap[i].signame) == 0) return sigmap[i].signum;
  }

err:
  errno = EINVAL;
  return -1;
}

char const *
signame(int signum)
{
  for (size_t i = 0; i < sizeof sigmap / sizeof *sigmap; ++i) {
    if (signum == sigmap[i].signum) return sigmap[i].signame;
  }
  return 0;
}
