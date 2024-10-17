#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
  size_t n = argc > 1 ? atoi(argv[1]) * 1024 : BUFSIZ;
  setbuf(stdin, NULL);
  setbuf(stdout, NULL);
  for (size_t i = 0; i < n; ++i) {
    int c = getchar();
    if (c == EOF) {
      if (feof(stdin)) exit(0);
      exit(1);
    }
    putchar(c);
  }
}
