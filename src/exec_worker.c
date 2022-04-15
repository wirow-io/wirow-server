#include <stdlib.h>
#include <stdint.h>

int mediasoup_worker_run(
  int argc,
  char *argv[],
  const char *version,
  int consumerChannelFd,
  int producerChannelFd,
  int payloadConsumeChannelFd,
  int payloadProduceChannelFd,
  void (*)(uint8_t**, uint32_t*, size_t*, const void*, void*),
  void*,
  void (*)(const uint8_t*, uint32_t, void*),
  void*,
  void (*)(uint8_t**, uint32_t*, size_t*, uint8_t**, uint32_t*, size_t*, void*, void*),
  void*,
  void (*)(const uint8_t*, uint32_t, const uint8_t*, uint32_t, void*),
  void*);

int exec_worker(int argc, char **argv) {
  return mediasoup_worker_run(argc, argv, "3.9.9", 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0);
}
