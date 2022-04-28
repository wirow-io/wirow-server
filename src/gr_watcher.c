/*
 * copyright (c) 2022 greenrooms, inc.
 *
 * this program is free software: you can redistribute it and/or modify it under
 * the terms of the gnu affero general public license as published by the free
 * software foundation, either version 3 of the license, or (at your option) any
 * later version.
 *
 * this program is distributed in the hope that it will be useful, but without
 * any warranty; without even the implied warranty of merchantability or fitness
 * for a particular purpose. see the gnu affero general public license for more
 * details.
 *
 * you should have received a copy of the gnu affero general public license
 * along with this program.  if not, see http://www.gnu.org/licenses/
 */

#include "gr_watcher.h"

#if defined(__linux__)

#include "grh_ws.h"
#include <iwnet/iwn_scheduler.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>

static volatile int _do_pending = 0;

static void _on_cancel(void *data) {
  __sync_bool_compare_and_swap(&_do_pending, 1, 0);
}

static void _inotify_do(void *data) {
  grh_ws_send_all("{\"cmd\":\"watcher\"}", -1);
  __sync_bool_compare_and_swap(&_do_pending, 1, 0);
}

static int64_t _on_ready(const struct iwn_poller_task *t, uint32_t events) {
  ssize_t len;
  char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
  for ( ; ; ) {
    len = read(t->fd, buf, sizeof(buf));
    if (len <= 0) {
      break;
    }
  }
  if (__sync_bool_compare_and_swap(&_do_pending, 0, 1)) {
    iwrc rc = iwn_schedule(&(struct iwn_scheduler_spec) {
      .poller = g_env.poller,
      .task_fn = _inotify_do,
      .on_cancel = _on_cancel,
      .timeout_ms = 800     // 0.8 sec
    });
    if (rc) {
      _do_pending = 0;
      iwlog_ecode_error3(rc);
    }
  }
  return 0;
}

iwrc gr_watcher_init(void) {
  const char **rr = g_env.private_overlays.watch;
  if (!rr) { // nothing to watch
    return 0;
  }
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd == -1) {
    return iwrc_set_errno(IW_ERROR_ERRNO, errno);
  }
  for ( ; *rr; ++rr) {
    const char *path = *rr;
    iwlog_info("\tWatching: %s", path);
    if (inotify_add_watch(fd, path, IN_MODIFY | IN_CREATE | IN_DELETE) == -1) {
      iwlog_ecode_error(iwrc_set_errno(IW_ERROR_ERRNO, errno), "Failed to add file to the watch list: %s", path);
    }
  }
  iwrc rc = iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = fd,
    .events = IWN_POLLIN,
    .events_mod = IWN_POLLET,
    .on_ready = _on_ready,
  });
  if (rc) {
    close(fd);
  }
  return rc;
}

#else

iwrc gr_watcher_init(void) {
  iwlog_warn("No gr_watcher mmodule on this system");
  return 0;
}

#endif
