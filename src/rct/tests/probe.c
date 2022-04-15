#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

static int fds[2] = { -1, -1 };

static void _on_signal(int signo) {
  fprintf(stderr, "Closing on SIGTERM\n");
  exit(0);
}

int main(int argc, char const *argv[]) {

  if (signal(SIGTERM, _on_signal) == SIG_ERR) {
    return EXIT_FAILURE;
  }

  for (int i = 0; i < argc; ++i) {
    fprintf(stdout, "%s\n", argv[i]);
  }
  fprintf(stdout, "--------------------------------------------------------------------------------\n");

  int ch = 0, rb;
  while ((ch = fgetc(stdin)) != EOF) {
    fputc(ch, stdout);
  }
  fflush(stdout);

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) {
    return EXIT_FAILURE;
  }
  read(fds[0], &rb, sizeof(rb));
  return 0;
}
