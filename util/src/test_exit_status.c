#define _POSIX_C_SOURCE 200809
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  return (argc > 1 ? atoi(argv[1]) : 0); 
}
