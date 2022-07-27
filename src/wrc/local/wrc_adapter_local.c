#include "../wrc_adapter.h"
#include "rct/rct.h"

#include <iwnet/iwn_proc.h>
#include <iowow/iwpool.h>
#include <iowow/iwxstr.h>

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

extern struct gr_env g_env;

#define FD_MSG_OUT_C     0 // 3
#define FD_MSG_OUT       1
#define FD_MSG_IN        2
#define FD_MSG_IN_C      3 // 4
#define FD_PAYLOAD_OUT_C 4 // 5
#define FD_PAYLOAD_OUT   5
#define FD_PAYLOAD_IN    6
#define FD_PAYLOAD_IN_C  7 // 6
#define FDS_NUM          8

#define READBUF_SIZE_MAX (1024 * 1024)

struct worker {
  rct_resource_base_t base;

  pid_t pid;
  struct wrc_adapter_spec spec;

  int fds[FDS_NUM];

  IWXSTR *msg_read_buf;
  IWXSTR *msg_write_buf;
  ssize_t msg_read_len;

  IWXSTR *payload_read_buf;
  IWXSTR *payload_write_buf;
  ssize_t payload_read_len;

  pthread_mutex_t mtx;
};

IW_INLINE void _fd_close(struct worker *w, int idx) {
  if (w->fds[idx] > -1) {
    close(w->fds[idx]);
    w->fds[idx] = -1;
  }
}

static void _fds_close(struct worker *w) {
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); ++i) {
    _fd_close(w, i);
  }
}

static void _on_channel_dispose(const struct iwn_poller_task *t) {
  struct worker *w = t->user_data;
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); ++i) {
    if (w->fds[i] == t->fd) {
      w->fds[i] = -1;
      break;
    }
  }
  iwn_proc_unref(w->pid);
}

static iwrc _fd_set_nb(int fd) {
  int rci, flags;
  while ((flags = fcntl(fd, F_GETFL, 0)) == -1 && errno == EINTR);
  if (flags == -1) {
    return iwrc_set_errno(IW_ERROR_ERRNO, errno);
  }
  while ((rci = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1 && errno == EINTR);
  if (rci == -1) {
    return iwrc_set_errno(IW_ERROR_ERRNO, errno);
  }
  return 0;
}

static iwrc _fds_init(struct worker *w) {
  iwrc rc = 0;
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); i += 2) {
    RCN(finish, pipe(&w->fds[i]));
  }
  RCC(rc, finish, _fd_set_nb(w->fds[FD_MSG_IN]));
  RCC(rc, finish, _fd_set_nb(w->fds[FD_MSG_OUT]));
  RCC(rc, finish, _fd_set_nb(w->fds[FD_PAYLOAD_OUT]));
  RCC(rc, finish, _fd_set_nb(w->fds[FD_PAYLOAD_IN]));

finish:
  return rc;
}

static void _destroy(struct worker *w) {
  _fds_close(w);
  iwxstr_destroy(w->payload_read_buf);
  iwxstr_destroy(w->payload_write_buf);
  iwxstr_destroy(w->msg_read_buf);
  iwxstr_destroy(w->msg_write_buf);
  pthread_mutex_destroy(&w->mtx);
}

static void _kill(pid_t pid) {
  iwn_proc_kill_ensure(g_env.poller, pid, SIGINT, -10, SIGKILL);
}

static void _on_stdout(const struct iwn_proc_ctx *ctx, const char *buf, size_t len) {
  if (g_env.log.verbose) {
    iwlog_info("WRC[%d]: %.*s", ctx->pid, (int) len, buf);
  }
}

static void _on_stderr(const struct iwn_proc_ctx *ctx, const char *buf, size_t len) {
  iwlog_info("WRC[%d]: %.*s", ctx->pid, (int) len, buf);
}

static void _on_exit(const struct iwn_proc_ctx *ctx) {
  int code = WIFEXITED(ctx->wstatus) ? WEXITSTATUS(ctx->wstatus) : -1;
  iwlog_info("WRC[%d] exited, code: %d", ctx->pid, code);
  struct worker *w = ctx->user_data;
  wrc_resource_t wid = w->base.id;
  w->pid = -1;
  w->base.close = 0;
  if (w->spec.on_closed) {
    w->spec.on_closed(wid, w->spec.user_data, 0);
  }
  rct_resource_ref_keep_locking(&w->base, false, -1, __func__);
  rct_resource_close(wid);
}

static int64_t _on_msg_write(const struct iwn_poller_task *t, uint32_t flags) {
  struct worker *w = t->user_data;

again:
  pthread_mutex_lock(&w->mtx);
  if (iwxstr_size(w->msg_write_buf) == 0) {
    pthread_mutex_unlock(&w->mtx);
    return 0;
  }

  ssize_t len = write(t->fd, iwxstr_ptr(w->msg_write_buf), iwxstr_size(w->msg_write_buf));

  if (len == -1) {
    pthread_mutex_unlock(&w->mtx);
    if (errno == EINTR) {
      goto again;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return IWN_POLLOUT;
    } else {
      iwlog_ecode_error(iwrc_set_errno(IW_ERROR_IO_ERRNO, errno), "WRC[%d] Error writing worker command data", w->pid);
      return 0;
    }
  } else {
    iwxstr_shift(w->msg_write_buf, len);
    pthread_mutex_unlock(&w->mtx);
    goto again;
  }

  pthread_mutex_unlock(&w->mtx);
  return 0;
}

static int64_t _on_payload_write(const struct iwn_poller_task *t, uint32_t flags) {
  struct worker *w = t->user_data;

again:
  pthread_mutex_lock(&w->mtx);
  if (iwxstr_size(w->payload_write_buf) == 0) {
    pthread_mutex_unlock(&w->mtx);
    return 0;
  }

  ssize_t len = write(t->fd, iwxstr_ptr(w->payload_write_buf), iwxstr_size(w->payload_write_buf));

  if (len == -1) {
    pthread_mutex_unlock(&w->mtx);
    if (errno == EINTR) {
      goto again;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return IWN_POLLOUT;
    } else {
      iwlog_ecode_error(iwrc_set_errno(IW_ERROR_IO_ERRNO, errno), "WRC[%d] Error writing worker payoad data", w->pid);
      return 0;
    }
  } else {
    iwxstr_shift(w->payload_write_buf, len);
    pthread_mutex_unlock(&w->mtx);
    goto again;
  }

  pthread_mutex_unlock(&w->mtx);
  return 0;
}

static int64_t _on_channel_read(
  const struct iwn_poller_task *t,
  IWXSTR                       *xstr,
  ssize_t                      *rlp,
  void (                       *on_msg )(wrc_resource_t wid,
                                         const char    *buf,
                                         size_t         len,
                                         void          *user_data)
  ) {
  iwrc rc = 0;
  ssize_t len;
  char buf[4096];
  struct worker *w = t->user_data;

again:
  len = read(t->fd, buf, sizeof(buf));
  if (len == -1) {
    if (errno == EINTR) {
      goto again;
    } else if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
      rc = iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
    }
    goto finish;
  }

  RCC(rc, finish, iwxstr_cat(xstr, buf, len));

again2:
  if (*rlp == -1) {
    if (iwxstr_size(xstr) >= 4) {
      uint32_t lv;
      memcpy(&lv, iwxstr_ptr(xstr), 4);
      lv = IW_ITOHL(lv);
      if (lv > READBUF_SIZE_MAX) {
        iwlog_error("WRC[%d] invalid data from message channel: data length exceeds the max buffer size: %d",
                    w->pid, READBUF_SIZE_MAX);
        return -1;
      }
      *rlp = lv;
      iwxstr_shift(xstr, 4);
    } else {
      goto again;
    }
  }

  if (*rlp <= iwxstr_size(xstr)) {
    char *msg;
    RCB(finish, msg = malloc(*rlp + 1));
    memcpy(msg, iwxstr_ptr(xstr), *rlp);
    msg[*rlp] = '\0';
    iwxstr_shift(xstr, *rlp);

    on_msg(w->base.id, msg, *rlp, w->spec.user_data);
    free(msg);

    *rlp = -1;
    if (iwxstr_size(xstr) > 0) {
      goto again2;
    }
  }

  goto again;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return 0;
}

static int64_t _on_msg_read(const struct iwn_poller_task *t, uint32_t flags) {
  struct worker *w = t->user_data;
  return _on_channel_read(t, w->msg_read_buf, &w->msg_read_len, w->spec.on_msg);
}

static int64_t _on_payload_read(const struct iwn_poller_task *t, uint32_t flags) {
  struct worker *w = t->user_data;
  return _on_channel_read(t, w->payload_read_buf, &w->payload_read_len, w->spec.on_payload);
}

static void _on_fork_parent(const struct iwn_proc_ctx *ctx, pid_t pid) {
  iwrc rc = 0;
  struct worker *w = ctx->user_data;
  w->pid = pid;

  _fd_close(w, FD_MSG_IN_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_MSG_IN],
    .user_data = w,
    .on_ready = _on_msg_read,
    .on_dispose = _on_channel_dispose,
    .events = IWN_POLLIN,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

  _fd_close(w, FD_PAYLOAD_IN_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_PAYLOAD_IN],
    .user_data = w,
    .on_ready = _on_payload_read,
    .on_dispose = _on_channel_dispose,
    .events = IWN_POLLIN,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

  _fd_close(w, FD_MSG_OUT_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_MSG_OUT],
    .user_data = w,
    .on_ready = _on_msg_write,
    .on_dispose = _on_channel_dispose,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

  _fd_close(w, FD_PAYLOAD_OUT_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_PAYLOAD_OUT],
    .user_data = w,
    .on_ready = _on_payload_write,
    .on_dispose = _on_channel_dispose,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    _kill(pid);
  }
}

static void _on_fork_child(const struct iwn_proc_ctx *ctx) {
  struct worker *w = ctx->user_data;
  assert(w);
  while ((dup2(w->fds[FD_MSG_OUT_C], 3) == -1) && (errno == EINTR));
  while ((dup2(w->fds[FD_MSG_IN_C], 4) == -1) && (errno == EINTR));
  while ((dup2(w->fds[FD_PAYLOAD_OUT_C], 5) == -1) && (errno == EINTR));
  while ((dup2(w->fds[FD_PAYLOAD_IN_C], 6) == -1) && (errno == EINTR));
  _fds_close(w);
}

static void _on_fork(const struct iwn_proc_ctx *ctx, pid_t pid) {
  if (pid > 0) {
    _on_fork_parent(ctx, pid);
  } else {
    _on_fork_child(ctx);
  }
}

static void _resource_close(void *w_) {
  struct worker *w = w_;
  if (w && w->pid > 0) {
    _kill(w->pid);
  }
}

static void _resource_dispose(void *w) {
  _destroy(w);
}

iwrc wrc_adapter_create(
  const struct wrc_adapter_spec *spec,
  wrc_resource_t                *out_wrc_id
  ) {
  *out_wrc_id = 0;

  iwrc rc = 0;
  IWXSTR *xargs = 0;
  bool locked = false;

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  struct worker *w = iwpool_calloc(sizeof(*w), pool);
  if (!w) {
    iwpool_destroy(pool);
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  w->base.pool = pool;
  w->base.type = RCT_TYPE_WORKER_ADAPTER;
  w->base.close = _resource_close;
  w->base.dispose = _resource_dispose;

  w->msg_read_len = -1;
  w->payload_read_len = -1;
  w->spec = *spec;

  pthread_mutex_init(&w->mtx, 0);

  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); ++i) {
    w->fds[i] = -1;
  }

  struct iwn_proc_spec ps = (struct iwn_proc_spec) {
    .poller = g_env.poller,
    .path = g_env.worker.program ? g_env.worker.program : g_env.program_file,
    .user_data = w,
    .on_stdout = _on_stdout,
    .on_stderr = _on_stderr,
    .on_exit = _on_exit,
    .on_fork = _on_fork
  };

  RCB(finish, xargs = iwxstr_new());
  RCB(finish, w->msg_read_buf = iwxstr_new());
  RCB(finish, w->msg_write_buf = iwxstr_new());
  RCB(finish, w->payload_read_buf = iwxstr_new());
  RCB(finish, w->payload_write_buf = iwxstr_new());

  if (ps.path == g_env.program_file) {
    RCC(rc, finish, iwxstr_cat2(xargs, "w"));
  }
  const char **logtags = g_env.worker.log_tags;
  for (int i = 0; logtags && logtags[i]; ++i) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1--logTags=%s", logtags[i]));
  }
  if (g_env.worker.log_level) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1--logLevel=%s", g_env.worker.log_level));
  }
  if (g_env.rtc.start_port) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1--rtcMinPort=%d", g_env.rtc.start_port));
  }
  if (g_env.rtc.end_port) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1--rtcMaxPort=%d", g_env.rtc.end_port));
  }
  if (g_env.dtls.cert_file) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1--dtlsCertificateFile=%s", g_env.dtls.cert_file));
  }
  if (g_env.dtls.cert_key_file) {
    RCC(rc, finish, iwxstr_printf(xargs, "\1--dtlsPrivateKeyFile=%s", g_env.dtls.cert_key_file));
  }
  RCB(finish, ps.args = iwpool_split_string(pool, iwxstr_ptr(xargs), "\1", true));
  if (g_env.log.verbose) {
    char *cmd = iwn_proc_command_get(&ps);
    if (cmd) {
      iwlog_info("WRC Worker spawn: %s", cmd);
    }
    free(cmd);
  }

  RCC(rc, finish, _fds_init(w));
  RCC(rc, finish, iwn_proc_spawn(&ps, &w->pid));
  rct_resource_ref_locked(w, RCT_INIT_REFS, __func__), locked = true;
  RCC(rc, finish, rct_resource_register_lk(w));

  *out_wrc_id = w->base.id;

finish:
  rct_resource_ref_unlock(w, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 2, __func__);
  iwxstr_destroy(xargs);
  return rc;
}

iwrc wrc_adapter_send_data(wrc_resource_t wid, void *buf, size_t len) {
  struct worker *w = rct_resource_by_id_unlocked(wid, RCT_TYPE_WORKER_ADAPTER, __func__);
  if (!w) {
    return IW_ERROR_INVALID_STATE;
  }

  pthread_mutex_lock(&w->mtx);
  iwrc rc = iwxstr_cat(w->msg_write_buf, buf, len);
  pthread_mutex_unlock(&w->mtx);

  if (!rc) {
    rc = iwn_poller_arm_events(g_env.poller, w->fds[FD_MSG_OUT], IWN_POLLOUT);
  }

  rct_resource_ref_keep_locking(w, false, -1, __func__);
  return rc;
}

iwrc wrc_adapter_send_payload(wrc_resource_t wid, void *buf, size_t len) {
  struct worker *w = rct_resource_by_id_unlocked(wid, RCT_TYPE_WORKER_ADAPTER, __func__);
  if (!w) {
    return IW_ERROR_INVALID_STATE;
  }

  pthread_mutex_lock(&w->mtx);
  iwrc rc = iwxstr_cat(w->payload_write_buf, buf, len);
  pthread_mutex_unlock(&w->mtx);

  if (!rc) {
    rc = iwn_poller_arm_events(g_env.poller, w->fds[FD_PAYLOAD_OUT], IWN_POLLOUT);
  }

  rct_resource_ref_keep_locking(w, false, -1, __func__);
  return rc;
}

void wrc_adapter_close(wrc_resource_t wid) {
  rct_resource_close(wid);
}

iwrc wrc_adapter_module_init(void) {
  return 0;
}

void wrc_adapter_module_shutdown(void) {
  rct_resource_close_of_type(RCT_TYPE_WORKER_ADAPTER);
}

void wrc_adapter_module_destroy(void) {
}
