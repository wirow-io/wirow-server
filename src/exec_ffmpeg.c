#include <stdlib.h>
#include "lic_env.h"

int ffmpeg_main(int argc, char **argv);

int exec_ffmpeg(int argc, char **argv) {
#if (ENABLE_RECORDING == 1)
  return ffmpeg_main(argc, argv);
#else
  return 0;
#endif
}
