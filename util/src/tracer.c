#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

#include <linux/ptrace.h> /* Additional ptrace consts; MUST be after sys/ptrace */

#include "signum.h"

static pid_t fork_and_trace(void);
static FILE *open_log(char const *fn);

struct io_args {
  int ptm;
  struct termios attr;
  long long olimit;
};

void *
io_thread(void *p)
{
  struct io_args *args = p;
  char buf[BUFSIZ];
  int done_reading = 0;
  for (;;) {
    struct pollfd fds[] = {
        {.fd = args->ptm, .events = POLLIN | POLLHUP, .revents = 0},
        {.fd = STDIN_FILENO, .events = POLLIN | POLLHUP, .revents = 0},
    };
    int evt_cnt = poll(fds, sizeof fds / sizeof *fds, -1);
    if (evt_cnt < 0) err(1, "io_thread");

    for (size_t i = 0; i < sizeof fds / sizeof *fds; ++i) {
      if (fds[i].revents & POLLNVAL) {
        errno = EBADF;
        err(1, "io_thread: %d", fds[i].fd);
      }
    }

    /* Output from tracee */
    if (fds[0].revents & (POLLIN | POLLHUP)) {
      ssize_t n = read(fds[0].fd, buf, sizeof buf);
      if (n < 0) {
        if (errno == EIO) {
          errno = 0;
          goto done;
        }
        err(1, "input_thread");
      }
      if (n == 0) {
        goto done;
      }

      if (args->olimit > 0) {
        if (n >= args->olimit) {
          n = args->olimit;
          args->olimit = 0;
          char const msg[] = "\n[output truncated]\n";
          for (size_t i = 0; i < sizeof msg - 1;) {
            int n = write(1, &msg[i], sizeof msg - 1 - i);
            if (n < 0) err(1, 0);
            i += n;
          }
        } else {
          args->olimit -= n;
        }
      }

      if (args->olimit != 0) {
        for (size_t i = 0; i < n;) {
          ssize_t w = write(STDOUT_FILENO, buf + i, n - i);
          if (w < 0) err(1, 0);
          i += w;
        }
      }
    }

    /* Input to tracee */
    if (!done_reading) {
      if (fds[1].revents & (POLLIN | POLLHUP)) {
        ssize_t n = read(fds[1].fd, buf, sizeof buf);
        if (n < 0) err(1, "io_thread input");
        if (n == 0) {
          if (args->attr.c_lflag & ICANON) {
            write(args->ptm, &args->attr.c_cc[VEOF], 1);
          }
          done_reading = 1;
        }
        for (size_t i = 0; i < n;) {
          ssize_t w = write(args->ptm, buf + i, n - i);
          if (w < 0) err(1, 0);
          i += w;
        }
      }
    }
  }
done:
  return 0;
}

int
main(int argc, char *argv[])
{
  FILE *logfile = stdout;
  FILE *errfile = stderr;
  long child_limit = -1;
  long long output_limit = -1;
  char const *uname = 0;

  int opt;
  while ((opt = getopt(argc, argv, "u:g:c:l:o:e:h")) != -1) {
    switch (opt) {
      case 'u':
        uname = optarg;
        break;
      case 'c': {
        char *eptr = optarg;
        child_limit = strtol(optarg, &eptr, 10);
        if (*optarg && *eptr || child_limit <= 0) {
          errno = EINVAL;
          err(1, "%s", optarg);
        }
      } break;
      case 'l': {
        char *eptr = optarg;
        output_limit = strtoll(optarg, &eptr, 10);
        if (*optarg && *eptr || output_limit <= 0) {
          errno = EINVAL;
          err(1, "%s", optarg);
        }
      } break;
      case 'o':
        logfile = open_log(optarg);
        if (!logfile) err(1, "%s", optarg);
        break;
      case 'e':
        errfile = open_log(optarg);
        if (!errfile) err(1, "%s", optarg);
        break;
      case 'h':;
        char *name = strrchr(argv[0], '/');
        if (!name) name = argv[0];
        else ++name;
        errx(0,
             "usage: %s [-c CHILD_LIMIT] [-o OUTPUT_LIMIT] [-o LOGFILE] [-e "
             "ERRFILE] cmd [args...]\n",
             name);
    }
  }
  int cmd_idx = optind;

  if (argc == optind) errx(1, "no command name");
  sigset_t new_sigmask, old_sigmask;
  sigfillset(&new_sigmask);
  if (sigprocmask(SIG_BLOCK, &new_sigmask, &old_sigmask) < 0) err(1, 0);

  int ptm = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (ptm < 0) err(1, "opening ptm failed");
  grantpt(ptm);
  unlockpt(ptm);
  pid_t const root_pid = fork_and_trace();
  if (root_pid == 0) {
    if (setsid() < -1) err(1, "setsid");

    char const *ptss = ptsname(ptm);
    if (!ptss) err(1, 0);
    close(STDIN_FILENO);
    int pts = open(ptss, O_RDWR);
    if (pts < 0) err(1, 0);
    if (ioctl(pts, TIOCSCTTY, 0) < 0) err(1, "ioctl TIOCSCTTY");
    if (tcsetpgrp(pts, getpgrp()) < 0) err(1, "tcsetpgrp");

    if (pts != STDIN_FILENO) {
      if (dup2(pts, STDIN_FILENO) < 0) err(1, 0);
      close(pts);
    }
    dup2(STDIN_FILENO, STDOUT_FILENO);
    dup2(STDIN_FILENO, STDERR_FILENO);

    if (sigprocmask(SIG_SETMASK, &old_sigmask, 0) < 0) err(1, 0);

    if (uname) {
      struct passwd *pw = getpwnam(uname);
      if (!pw) err(1, "%s", uname);
      if (setgroups(0, 0) < 0) err(1, "setgroups");
      if (setgid(pw->pw_gid) < 0) err(1, "setgid");
      if (setuid(pw->pw_uid) < 0) err(1, "setuid");
    }

    execvp(argv[cmd_idx], &argv[cmd_idx]);
    err(1, "%s", argv[cmd_idx]);
  }

  struct termios attr;
  tcgetattr(ptm, &attr);
  attr.c_lflag |= ICANON | ISIG | ECHO | ECHONL;
  attr.c_iflag |= IUTF8;
  tcsetattr(ptm, TCSANOW, &attr);

  pthread_t io_tid;
  struct io_args io_args = {.ptm = ptm, .attr = attr, .olimit = output_limit};
  pthread_create(&io_tid, 0, io_thread, &io_args);

  int sigfd = signalfd(-1, &new_sigmask, SFD_CLOEXEC);

  fprintf(logfile,
          "%jd\t%jd\ttrace_child\t%jd\n",
          (intmax_t)time(0),
          (intmax_t)getpid(),
          (intmax_t)root_pid);
  size_t n_children = 1;
  pid_t *children = malloc(sizeof *children * n_children);
  children[0] = root_pid;

  for (;;) {
    int s;
    pid_t tracee_pid = waitpid(-1, &s, 0);
    if (tracee_pid <= 0) {
      if (errno == EAGAIN) {
        errno = 0;
        continue;
      } else if (errno == ECHILD) {
        fprintf(errfile, "No children\n");
        goto errorexit;
      }
      goto exit;
    }
    if (WIFEXITED(s)) {
      fprintf(logfile,
              "%jd\t%jd\texit_status\t%d\n",
              (intmax_t)time(0),
              (intmax_t)tracee_pid,
              WEXITSTATUS(s));
      if (tracee_pid == root_pid) goto rootexit;
    } else if (WIFSIGNALED(s)) {
      fprintf(logfile,
              "%jd\t%jd\tterm_sig\t%d\t%s\n",
              (intmax_t)time(0),
              (intmax_t)tracee_pid,
              WTERMSIG(s),
              signame(WTERMSIG(s)));
      if (tracee_pid == root_pid) goto rootexit;
    } else if (WIFSTOPPED(s)) {
      /* Check for a fork */
      if (s >> 8 == (SIGTRAP | (PTRACE_EVENT_CLONE << 8)) ||
          s >> 8 == (SIGTRAP | (PTRACE_EVENT_FORK << 8)) ||
          s >> 8 == (SIGTRAP | (PTRACE_EVENT_VFORK << 8))) {
        unsigned long tracee_child_pid;
        ptrace(PTRACE_GETEVENTMSG, tracee_pid, 0, &tracee_child_pid);
        fprintf(logfile,
                "%jd\t%jd\tfork_child\t%jd\n",
                (intmax_t)time(0),
                (intmax_t)tracee_pid,
                (intmax_t)tracee_child_pid);

        ++n_children;
        void *tmp = realloc(children, sizeof *children * n_children);
        if (!tmp) goto errorexit;
        children = tmp;
        children[n_children - 1] = tracee_child_pid;
        if (child_limit > 0 && n_children > child_limit) {
          fprintf(errfile, "Reached descendant limit %ld\n", child_limit);
          goto errorexit;
        }
        ptrace(PTRACE_CONT, tracee_pid, 0, 0);
      } else if (s >> 8 == (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
        ptrace(PTRACE_CONT, tracee_pid, 0, 0);
      } else if (s >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8))) {
        size_t i = 0;
        for (; i < n_children; ++i) {
          if (tracee_pid == children[i]) {
            break;
          }
        }
        if (i == n_children) {
          fprintf(errfile,
                  "untraced descendant %jd exited\n",
                  (intmax_t)tracee_pid);
          goto errorexit;
        }
        --n_children;
        memcpy(&children[i],
               &children[i + 1],
               (n_children - i) * sizeof children[0]);
        ptrace(PTRACE_CONT, tracee_pid, 0, 0);
      } else if (s >> 8 == (SIGTRAP | (PTRACE_EVENT_STOP << 8))) {
        ptrace(PTRACE_CONT, tracee_pid, 0, 0);
      } else {
        fprintf(logfile,
                "%jd\t%jd\tsignaled\t%d\t%s\n",
                (intmax_t)time(0),
                (intmax_t)tracee_pid,
                WSTOPSIG(s),
                signame(WSTOPSIG(s)));
        ptrace(PTRACE_CONT, tracee_pid, 0, WSTOPSIG(s));
      }
    }
  }
errorexit:
  fprintf(logfile,
          "%jd\t%jd\ttrace_error\n",
          (intmax_t)time(0),
          (intmax_t)getpid());
  goto exit;
rootexit:
  fprintf(
      logfile, "%jd\t%jd\ttrace_end\n", (intmax_t)time(0), (intmax_t)getpid());
  goto exit;
exit:;
  ptrace(PTRACE_DETACH, root_pid, 0, 0);
  kill(root_pid, SIGKILL);
  if (n_children) {
    fprintf(errfile, "Killing %zu leftover descendants:\n", n_children);
    for (size_t i = 0; i < n_children; ++i) {
      fprintf(logfile,
              "%jd\n%jd\tkilled\n",
              (intmax_t)time(0),
              (intmax_t)children[i]);
      fprintf(errfile, "Killed child pid %jd\n", (intmax_t)children[i]);
      kill(children[i], SIGKILL);
    }
  }
  free(children);
  pthread_cancel(io_tid);
  pthread_join(io_tid, 0);
  return 0;
}

static pid_t
fork_and_trace(void)
{

  pid_t pid = fork();
  if (pid < 0) {
    err(1, "fork");
  } else if (pid == 0) {
    /* Child waits to be traced */
    raise(SIGSTOP);
    return pid;
  }

  for (;;) {
    pid_t wait_res;
    int status;
    wait_res = waitpid(pid, &status, WUNTRACED);

    if (wait_res <= 0) {
      if (errno == EINTR) {
        errno = 0;
        continue;
      } else {
        err(1, "child lost");
      }
    }

    if (WIFSTOPPED(status)) {
      /* Child stopped, seize it */
      int flags = PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXIT |
                  PTRACE_O_TRACEEXEC | PTRACE_O_TRACECLONE |
                  PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_EXITKILL;

      if (ptrace(PTRACE_SEIZE, pid, 0, flags) < 0) {
        err(1, "setting trace options");
      }
      if (ptrace(PTRACE_CONT, pid, 0, 0) < 0) {
        err(1, "starting trace");
      }
      return pid;
    } else if (WIFEXITED(status)) {
      errx(1, "child exited %d before it could be traced", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      errx(1,
           "child terminated by signal %d before it could be traced",
           WTERMSIG(status));
    }
  }
}

static FILE *
open_log(char const *fn)
{
  int fd = open(fn, O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) err(1, "%s", fn);
  FILE *logfile = fdopen(fd, "w");
  if (!logfile) err(1, "%s", fn);
  setbuf(logfile, 0);
  return logfile;
}
