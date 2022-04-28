/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include "wrc.h"
#include "gr_gauges.h"

#include <iwnet/iwn_proc.h>
#include <iwnet/iwn_scheduler.h>
#include <iowow/iwxstr.h>
#include <iowow/iwpool.h>
#include <iowow/iwarr.h>
#include <iowow/iwstw.h>
#include <iowow/iwp.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <stdatomic.h>

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

#define WRC_MSG_COMPLETE_HANDLER(msg_)            \
  if ((msg_)->mm.handler) {                      \
    (msg_)->mm.handler((void*) msg_);            \
  } else {                                        \
    _msg_destroy(msg_);                           \
  }

struct _event_lsnr {
  wrc_event_handler_t id;
  wrc_event_handler   h;
  void *op;
};

struct _event_work {
  wrc_event_e    evt;
  wrc_resource_t resource_id;
  JBL data;
  struct _msg *m;
};

struct _msg {
  struct wrc_msg  mm;
  struct _msg    *next_reply;
  pthread_mutex_t cond_mtx;
  pthread_cond_t  cond_completed;
  uint64_t ts;
  uint32_t id;
};

wrc_resource_t (*_uuid_resolver)(const char *uuid);

static struct _worker {
  wrc_resource_t id;
  int fds[FDS_NUM];

  IWPOOL *pool;

  IWXSTR *writebuf_msg;
  IWXSTR *readbuf_msg;
  ssize_t readlen_msg;

  IWXSTR *writebuf_payload;
  IWXSTR *readbuf_payload;
  ssize_t readlen_payload;

  JBL ongoing_payload_notification;

  pthread_mutex_t mtx;
  struct _worker *next;

  uint64_t load_score_zerotime;   // Time when worker load score reached zero value
  uint32_t refs;
  int      load_score;            // Worker load score rank

  atomic_bool shutdown;
} *_workers;

static pthread_mutex_t _mtx;

static IWULIST _listeners;

static IWSTW _stw;

static struct _msg *_reply, *_reply_tail;

static atomic_bool _shutdown_pending;

static atomic_int _num_workers;

static const char* _worker_cmd_name(wrc_worker_cmd_e cmd) {
  switch (cmd) {
    case WRC_CMD_WORKER_DUMP:
      return "worker.dump";
    case WRC_CMD_WORKER_GET_RESOURCE_USAGE:
      return "worker.getResourceUsage";
    case WRC_CMD_WORKER_UPDATE_SETTINGS:
      return "worker.updateSettings";
    case WRC_CMD_WORKER_ROUTER_CREATE:
      return "worker.createRouter";
    case WRC_CMD_ROUTER_CLOSE:
      return "router.close";
    case WRC_CMD_ROUTER_DUMP:
      return "router.dump";
    case WRC_CMD_ROUTER_CREATE_WEBRTC_TRANSPORT:
      return "router.createWebRtcTransport";
    case WRC_CMD_ROUTER_CREATE_PLAIN_TRANSPORT:
      return "router.createPlainTransport";
    case WRC_CMD_ROUTER_CREATE_PIPE_TRANSPORT:
      return "router.createPipeTransport";
    case WRC_CMD_ROUTER_CREATE_DIRECT_TRANSPORT:
      return "router.createDirectTransport";
    case WRC_CMD_ROUTER_CREATE_AUDIO_LEVEL_OBSERVER:
      return "router.createAudioLevelObserver";
    case WRC_CMD_ROUTER_CREATE_ACTIVE_SPEAKER_OBSERVER:
      return "router.createActiveSpeakerObserver";
    case WRC_CMD_TRANSPORT_CLOSE:
      return "transport.close";
    case WRC_CMD_TRANSPORT_DUMP:
      return "transport.dump";
    case WRC_CMD_TRANSPORT_GET_STATS:
      return "transport.getStats";
    case WRC_CMD_TRANSPORT_CONNECT:
      return "transport.connect";
    case WRC_CMD_TRANSPORT_SET_MAX_INCOMING_BITRATE:
      return "transport.setMaxIncomingBitrate";
    case WRC_CMD_TRANSPORT_RESTART_ICE:
      return "transport.restartIce";
    case WRC_CMD_TRANSPORT_PRODUCE:
      return "transport.produce";
    case WRC_CMD_TRANSPORT_CONSUME:
      return "transport.consume";
    case WRC_CMD_TRANSPORT_PRODUCE_DATA:
      return "transport.produceData";
    case WRC_CMD_TRANSPORT_CONSUME_DATA:
      return "transport.consumeData";
    case WRC_CMD_TRANSPORT_ENABLE_TRACE_EVENT:
      return "transport.enableTraceEvent";
    case WRC_CMD_PRODUCER_CLOSE:
      return "producer.close";
    case WRC_CMD_PRODUCER_DUMP:
      return "producer.dump";
    case WRC_CMD_PRODUCER_GET_STATS:
      return "producer.getStats";
    case WRC_CMD_PRODUCER_PAUSE:
      return "producer.pause";
    case WRC_CMD_PRODUCER_RESUME:
      return "producer.resume";
    case WRC_CMD_PRODUCER_ENABLE_TRACE_EVENT:
      return "producer.enableTraceEvent";
    case WRC_CMD_CONSUMER_CLOSE:
      return "consumer.close";
    case WRC_CMD_CONSUMER_DUMP:
      return "consumer.dump";
    case WRC_CMD_CONSUMER_GET_STATS:
      return "consumer.getStats";
    case WRC_CMD_CONSUMER_PAUSE:
      return "consumer.pause";
    case WRC_CMD_CONSUMER_RESUME:
      return "consumer.resume";
    case WRC_CMD_CONSUMER_SET_PREFERRED_LAYERS:
      return "consumer.setPreferredLayers";
    case WRC_CMD_CONSUMER_SET_PRIORITY:
      return "consumer.setPriority";
    case WRC_CMD_CONSUMER_REQUEST_KEY_FRAME:
      return "consumer.requestKeyFrame";
    case WRC_CMD_CONSUMER_ENABLE_TRACE_EVENT:
      return "consumer.enableTraceEvent";
    case WRC_CMD_DATA_PRODUCER_CLOSE:
      return "dataProducer.close";
    case WRC_CMD_DATA_PRODUCER_DUMP:
      return "dataProducer.dump";
    case WRC_CMD_DATA_PRODUCER_GET_STATS:
      return "dataProducer.getStats";
    case WRC_CMD_DATA_CONSUMER_CLOSE:
      return "dataConsumer.close";
    case WRC_CMD_DATA_CONSUMER_DUMP:
      return "dataConsumer.dump";
    case WRC_CMD_DATA_CONSUMER_GET_STATS:
      return "dataConsumer.getStats";
    case WRC_CMD_DATA_CONSUMER_GET_BUFFERED_AMOUNT:
      return "dataConsumer.getBufferedAmount";
    case WRC_CMD_DATA_CONSUMER_SET_BUFFERED_AMOUNT_LOW_THRESHOLD:
      return "dataConsumer.setBufferedAmountLowThreshold";
    case WRC_CMD_RTP_OBSERVER_CLOSE:
      return "rtpObserver.close";
    case WRC_CMD_RTP_OBSERVER_PAUSE:
      return "rtpObserver.pause";
    case WRC_CMD_RTP_OBSERVER_RESUME:
      return "rtpObserver.resume";
    case WRC_CMD_RTP_OBSERVER_ADD_PRODUCER:
      return "rtpObserver.addProducer";
    case WRC_CMD_RTP_OBSERVER_REMOVE_PRODUCER:
      return "rtpObserver.removeProducer";
    default:
      return 0;
  }
}

IW_INLINE void _msg_data_destroy(struct wrc_msg *m) {
  switch (m->type) {
    case WRC_MSG_WORKER: {
      jbl_destroy(&m->input.worker.data);
      jbl_destroy(&m->input.worker.internal);
      jbl_destroy(&m->output.worker.data);
      break;
    }
    case WRC_MSG_PAYLOAD:
      jbl_destroy(&m->input.payload.data);
      jbl_destroy(&m->input.payload.internal);
      free(m->input.payload.payload);
      m->input.payload.payload = 0;
      break;
    case WRC_MSG_EVENT:
      jbl_destroy(&m->output.event.data);
      break;
    default:
      break;
  }
}

static void _msg_destroy(struct _msg *m) {
  if (m) {
    _msg_data_destroy(&m->mm);
    pthread_mutex_destroy(&m->cond_mtx);
    pthread_cond_destroy(&m->cond_completed);
    free(m);
  }
}

void wrc_msg_destroy(struct wrc_msg *msg) {
  _msg_destroy((void*) msg);
}

static void _worker_kill_lk(struct _worker *w) {
  w->shutdown = true;
  iwn_proc_kill_ensure(g_env.poller, w->id, SIGINT, -10, SIGKILL);
  for (struct _msg *m = _reply; m; m = m->next_reply) {
    if (m->mm.resource_id == w->id) {
      m->mm.rc = GR_ERROR_WORKER_EXIT;
      pthread_mutex_lock(&m->cond_mtx);
      pthread_cond_broadcast(&m->cond_completed);
      pthread_mutex_unlock(&m->cond_mtx);
    }
  }
}

IW_INLINE void _worker_close_fd(struct _worker *w, int idx) {
  if (w->fds[idx] > -1) {
    close(w->fds[idx]);
    w->fds[idx] = -1;
  }
}

static void _worker_close_fds(struct _worker *w) {
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); ++i) {
    _worker_close_fd(w, i);
  }
}

static void _worker_register_lk(struct _worker *w) {
  w->next = _workers;
  _workers = w;
  ++_num_workers;
  gr_gauge_set_async(GAUGE_WORKERS, _num_workers);
}

static void _worker_unregister_lk(struct _worker *w) {
  int nw = _num_workers;
  for (struct _worker *ww = _workers, *pp = 0; ww; pp = ww, ww = ww->next) {
    if (ww == w) {
      --_num_workers;
      if (pp) {
        pp->next = ww->next;
      } else {
        _workers = ww->next;
      }
      break;
    }
  }
  if (nw != _num_workers) {
    gr_gauge_set_async(GAUGE_WORKERS, _num_workers);
  }
}

static void _worker_destroy_lk(struct _worker *w) {
  _worker_unregister_lk(w);
  _worker_close_fds(w);
  iwxstr_destroy(w->writebuf_msg);
  iwxstr_destroy(w->writebuf_payload);
  iwxstr_destroy(w->readbuf_payload);
  iwxstr_destroy(w->readbuf_msg);
  jbl_destroy(&w->ongoing_payload_notification);
  pthread_mutex_destroy(&w->mtx);
  iwpool_destroy(w->pool);
}

static struct _worker* _worker_find_lk(wrc_resource_t id) {
  struct _worker *w = _workers;
  while (w) {
    if (w->id == id) {
      return w->shutdown ? 0 : w;
    }
    w = w->next;
  }
  return 0;
}

static struct _worker* _worker_find_ref(wrc_resource_t id) {
  pthread_mutex_lock(&_mtx);
  struct _worker *w = _worker_find_lk(id);
  if (w) {
    ++w->refs;
  }
  pthread_mutex_unlock(&_mtx);
  return w;
}

static void _worker_unref(struct _worker *w) {
  pthread_mutex_lock(&_mtx);
  if (--w->refs == 0) {
    _worker_destroy_lk(w);
  }
  pthread_mutex_unlock(&_mtx);
}

static int64_t _on_channel_read(
  const struct iwn_poller_task *t, IWXSTR *xstr, ssize_t *rlp,
  void (*on_msg)(struct _worker*, char*, size_t)
  ) {
  iwrc rc = 0;
  struct _worker *w = t->user_data;
  ssize_t len;
  char buf[4096];

again:
  len = read(t->fd, buf, sizeof(buf));
  if (len == -1) {
    if (errno == EINTR) {
      goto again;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      rc = 0;
    } else {
      rc = iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
    }
    goto finish;
  }

  RCC(rc, finish, iwxstr_cat(xstr, buf, len));

again2:
  if (*rlp == -1) {
    if (len >= 4) {
      uint32_t lv;
      memcpy(&lv, iwxstr_ptr(xstr), 4);
      lv = IW_ITOHL(lv);
      if (lv > READBUF_SIZE_MAX) {
        iwlog_error("WRC[%d] invalid data from message channel: data length exceeds the max buffer size: %d",
                    w->id, READBUF_SIZE_MAX);
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
    on_msg(w, msg, *rlp);
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

static void _notify_event_handlers_worker(void *arg) {
  IWULIST *list;
  struct _event_work *ew = arg;

  pthread_mutex_lock(&_mtx);
  list = iwulist_clone(&_listeners);
  pthread_mutex_unlock(&_mtx);

  for (size_t i = 0, l = iwulist_length(list); i < l; ++i) {
    struct _event_lsnr *n = iwulist_at2(list, i);
    assert(n);
    if (n->h) {
      iwrc rc = n->h(ew->evt, ew->resource_id, ew->data, n->op);
      if (rc) {
        iwlog_ecode_error(rc, "Failed to call event handler:%d", n->id);
      }
    }
  }

  if (ew->m) {
    ew->m->mm.output.event.evt = ew->evt;
    ew->m->mm.output.event.data = ew->data;
    ew->m->mm.output.event.resource_id = ew->resource_id;
    WRC_MSG_COMPLETE_HANDLER(ew->m);
  }

  iwulist_destroy(&list);
  jbl_destroy(&ew->data);
  free(ew);
}

static iwrc _notify_event_handlers(wrc_event_e evt, wrc_resource_t resource_id, JBL data, struct _msg *m) {
  iwrc rc = 0;
  struct _event_work *ew;

  RCB(finish, ew = malloc(sizeof(*ew)));
  *ew = (struct _event_work) {
    .evt = evt,
    .resource_id = resource_id,
    .data = data,
    .m = m
  };

  rc = iwstw_schedule(_stw, _notify_event_handlers_worker, ew);

finish:
  if (rc) {
    if (m) {
      m->mm.rc = rc;
      WRC_MSG_COMPLETE_HANDLER(m);
    }
    free(ew);
  }
  return rc;
}

static void _stalled_messages_cleanup_worker(void *d) {
  if (_shutdown_pending) {
    return;
  }
  iwrc rc;
  uint64_t ts;
  IWULIST list;
  int tms = g_env.worker.command_timeout_sec;
  if (tms < 1 || iwulist_init(&list, 8, sizeof(struct _msg*))) {
    goto arm;
  }
  iwp_current_time_ms(&ts, true);

  pthread_mutex_lock(&_mtx);
  for (struct _msg *m = _reply, *p = 0; m; m = m->next_reply) {
    if (ts > m->ts && ts - m->ts > 1000ULL * tms) {
      m->mm.rc = GR_ERROR_WORKER_COMMAND_TIMEOUT;
      if (p) {
        p->next_reply = m->next_reply;
        if (m->next_reply == 0) {
          _reply_tail = p;
        }
      } else {
        _reply = m->next_reply;
        if (m->next_reply == 0) {
          _reply_tail = 0;
        }
      }
      iwulist_push(&list, &m);
    } else {
      p = m;
    }
  }
  pthread_mutex_unlock(&_mtx);

  for (size_t i = 0, l = iwulist_length(&list); i < l; ++i) {
    struct _msg *m = *(struct _msg**) iwulist_at2(&list, i);
    if (m) {
      iwlog_warn("WRC message: %d reply timeout", m->id);
      m->mm.rc = GR_ERROR_WORKER_COMMAND_TIMEOUT;
      WRC_MSG_COMPLETE_HANDLER(m);
    }
  }
  iwulist_destroy_keep(&list);

arm:
  if (!_shutdown_pending) {
    rc = iwn_schedule(&(struct iwn_scheduler_spec) {
      .poller = g_env.poller,
      .timeout_ms = 1000U,
      .task_fn = _stalled_messages_cleanup_worker
    });
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }
}

static void _on_payload(struct _worker *w, char *buf, size_t len) {
  iwrc rc = 0;
  JBL jbl_notify = 0;

  pthread_mutex_lock(&w->mtx);
  if (w->ongoing_payload_notification) {
    jbl_notify = w->ongoing_payload_notification;
    w->ongoing_payload_notification = 0;
    char *payload = malloc(len + 5);
    if (payload) {
      uint32_t plen = len;
      memcpy(payload, &plen, 4);
      memcpy(payload + 4, buf, len);
      payload[len + 4] = '\0';
      jbl_set_user_data(jbl_notify, payload, free);
    }
  } else {
    JBL jbl;
    rc = jbl_from_json(&jbl, buf);
    if (rc) {
      iwlog_ecode_error3(rc);
    } else {
      w->ongoing_payload_notification = jbl;
    }
  }
  pthread_mutex_unlock(&w->mtx);

  if (jbl_notify) {
    _notify_event_handlers(WRC_EVT_PAYLOAD, w->id, jbl_notify, 0);
  }
}

static void _on_msg(struct _worker *w, char *cmd, size_t cmd_len) {
  iwrc rc = 0;
  JBL jbl = 0;
  int64_t id = 0;
  struct _msg *hm = 0;
  bool data_with_event = false;

  switch (*cmd) {
    case 'D':
      iwlog_info("WRC<%d> %s", w->id, cmd + 1);
      break;
    case 'W':
      iwlog_warn("WRC<%d> %s", w->id, cmd + 1);
      break;
    case 'E':
      iwlog_error("WRC<%d> %s", w->id, cmd + 1);
      break;
    case 'X':
      iwlog_info("WRC<%d> %s", w->id, cmd + 1);
      break;
    case '{':
      if (g_env.log.verbose) {
        iwlog_info("WRC<%d> %s\n", w->id, cmd);
      }
      RCC(rc, finish, jbl_from_json(&jbl, cmd));
      break;
    default:
      rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
      goto finish;
  }

  if (jbl) {
    wrc_resource_t target_resource = 0;
    const char *event = 0, *target_id = 0;
    if (jbl_object_get_type(jbl, "id") == JBV_I64) {
      RCC(rc, finish, jbl_object_get_i64(jbl, "id", &id));
    }
    // {"error":"Error","id":1,"reason":"request has no internal.routerId"}
    // {"accepted":true,"id":1}
    // {"event":"running","targetId":"25968"}
    if (jbl_object_get_type(jbl, "event") == JBV_STR) {
      RCC(rc, finish, jbl_object_get_str(jbl, "event", &event));
      if (jbl_object_get_type(jbl, "targetId") == JBV_STR) {
        RCC(rc, finish, jbl_object_get_str(jbl, "targetId", &target_id));
      }
    } else if (jbl_object_get_type(jbl, "error") == JBV_STR) {
      rc = GR_ERROR_WORKER_ANSWER;
    }

    // notify message handlers
    if (id > 0) {
      pthread_mutex_lock(&_mtx);
      for (struct _msg *m = _reply, *p = 0; m; p = m, m = m->next_reply) {
        if (m->id == id) {
          if (p) {
            p->next_reply = m->next_reply;
            if (m->next_reply == 0) {
              _reply_tail = p;
            }
          } else {
            _reply = m->next_reply;
            if (m->next_reply == 0) {
              _reply_tail = 0;
            }
          }
          hm = (void*) m;
          break;
        }
      }
      pthread_mutex_unlock(&_mtx);
    }

    if (target_id && _uuid_resolver) {
      target_resource = _uuid_resolver(target_id);
    }

    if (event) {
      if (!strcmp(event, "running")) {
        rc = _notify_event_handlers(WRC_EVT_WORKER_LAUNCHED, w->id, 0, 0);
        goto finish;
      }
      if (!target_resource) {
        goto finish;
      }
      data_with_event = true;
      if (!strcmp(event, "score")) {
        rc = _notify_event_handlers(WRC_EVT_RESOURCE_SCORE, target_resource, jbl, 0);
      } else if (!strcmp(event, "trace")) {
        rc = _notify_event_handlers(WRC_EVT_TRACE, target_resource, jbl, 0);
      } else if (!strcmp(event, "tuple")) {
        rc = _notify_event_handlers(WRC_EVT_TRANSPORT_TUPLE, target_resource, jbl, 0);
      } else if (!strcmp(event, "rtcptuple")) {
        rc = _notify_event_handlers(WRC_EVT_TRANSPORT_RTCPTUPLE, target_resource, jbl, 0);
      } else if (!strcmp(event, "silence")) {
        rc = _notify_event_handlers(WRC_EVT_AUDIO_OBSERVER_SILENCE, target_resource, 0, 0), data_with_event = false;
      } else if (!strcmp(event, "volumes")) {
        rc = _notify_event_handlers(WRC_EVT_AUDIO_OBSERVER_VOLUMES, target_resource, jbl, 0);
      } else if (!strcmp(event, "dominantspeaker")) {
        rc = _notify_event_handlers(WRC_EVT_ACTIVE_SPEAKER, target_resource, jbl, 0);
      } else if (!strcmp(event, "icestatechange")) {
        rc = _notify_event_handlers(WRC_EVT_TRANSPORT_ICE_STATE_CHANGE, target_resource, jbl, 0);
      } else if (!strcmp(event, "iceselectedtuplechange")) {
        rc = _notify_event_handlers(WRC_EVT_TRANSPORT_ICE_SELECTED_TUPLE_CHANGE, target_resource, jbl, 0);
      } else if (!strcmp(event, "dtlsstatechange")) {
        rc = _notify_event_handlers(WRC_EVT_TRANSPORT_DTLS_STATE_CHANGE, target_resource, jbl, 0);
      } else if (!strcmp(event, "sctpstatechange")) {
        rc = _notify_event_handlers(WRC_EVT_TRANSPORT_SCTP_STATE_CHANGE, target_resource, jbl, 0);
      } else if (!strcmp(event, "videoorientationchange")) {
        rc = _notify_event_handlers(WRC_EVT_PRODUCER_VIDEO_ORIENTATION_CHANGE, target_resource, jbl, 0);
      } else if (!strcmp(event, "producerclose")) {
        rc = _notify_event_handlers(WRC_EVT_CONSUMER_CLOSED, target_resource, 0, 0), data_with_event = false;
      } else if (!strcmp(event, "producerpause")) {
        rc = _notify_event_handlers(WRC_EVT_CONSUMER_PRODUCER_PAUSE, target_resource, 0, 0),
        data_with_event = false;
      } else if (!strcmp(event, "producerresume")) {
        rc = _notify_event_handlers(WRC_EVT_CONSUMER_PRODUCER_RESUME, target_resource, 0, 0),
        data_with_event = false;
      } else if (!strcmp(event, "layerschange")) {
        rc = _notify_event_handlers(WRC_EVT_CONSUMER_LAYERSCHANGE, target_resource, jbl, 0);
      } else {
        data_with_event = false;
      }
      // TODO:
      // sctpsendbufferfull
      // bufferedamountlow
    }
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  if (hm) {
    hm->mm.output.worker.data = jbl;
    WRC_MSG_COMPLETE_HANDLER(hm);
  } else if (rc || !data_with_event) {
    jbl_destroy(&jbl);
  }
}

static int64_t _on_msg_read(const struct iwn_poller_task *t, uint32_t flags) {
  struct _worker *w = t->user_data;
  assert(w);
  return _on_channel_read(t, w->readbuf_msg, &w->readlen_msg, _on_msg);
}

static int64_t _on_payload_read(const struct iwn_poller_task *t, uint32_t flags) {
  struct _worker *w = t->user_data;
  assert(w);
  return _on_channel_read(t, w->readbuf_payload, &w->readlen_payload, _on_payload);
}

static int64_t _on_msg_write(const struct iwn_poller_task *t, uint32_t flags) {
  struct _worker *w = t->user_data;
  assert(w);

again:
  pthread_mutex_lock(&w->mtx);
  if (iwxstr_size(w->writebuf_msg) == 0) {
    pthread_mutex_unlock(&w->mtx);
    return 0;
  }

  ssize_t len = write(t->fd, iwxstr_ptr(w->writebuf_msg), iwxstr_size(w->writebuf_msg));

  if (len == -1) {
    pthread_mutex_unlock(&w->mtx);
    if (errno == EINTR) {
      goto again;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    } else {
      iwlog_ecode_error(iwrc_set_errno(IW_ERROR_IO_ERRNO, errno), "WRC[%d] Error writing worker command data", w->id);
      return 0;
    }
  } else {
    iwxstr_shift(w->writebuf_msg, len);
    pthread_mutex_unlock(&w->mtx);
    goto again;
  }

  pthread_mutex_unlock(&w->mtx);
  return 0;
}

static int64_t _on_payload_write(const struct iwn_poller_task *t, uint32_t flags) {
  struct _worker *w = t->user_data;
  assert(w);

again:
  pthread_mutex_lock(&w->mtx);
  if (iwxstr_size(w->writebuf_payload) == 0) {
    pthread_mutex_unlock(&w->mtx);
    return 0;
  }

  ssize_t len = write(t->fd, iwxstr_ptr(w->writebuf_payload), iwxstr_size(w->writebuf_payload));

  if (len == -1) {
    pthread_mutex_unlock(&w->mtx);
    if (errno == EINTR) {
      goto again;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    } else {
      iwlog_ecode_error(iwrc_set_errno(IW_ERROR_IO_ERRNO, errno), "WRC[%d] Error writing worker payoad data", w->id);
      return 0;
    }
  } else {
    iwxstr_shift(w->writebuf_payload, len);
    pthread_mutex_unlock(&w->mtx);
    goto again;
  }

  pthread_mutex_unlock(&w->mtx);
  return 0;
}

static void _on_exit(const struct iwn_proc_ctx *ctx) {
  int code = WIFEXITED(ctx->wstatus) ? WEXITSTATUS(ctx->wstatus) : -1;
  iwlog_info("WRC[%d] exited, code: %d", ctx->pid, code);
  struct _worker *w = ctx->user_data;
  assert(w);
  w->shutdown = true;

  pthread_mutex_lock(&_mtx);
  for (struct _msg *m = _reply; m; m = m->next_reply) {
    if (m->mm.resource_id == w->id) {
      m->mm.rc = GR_ERROR_WORKER_EXIT;
      pthread_mutex_lock(&m->cond_mtx);
      pthread_cond_broadcast(&m->cond_completed);
      pthread_mutex_unlock(&m->cond_mtx);
    }
  }
  pthread_mutex_unlock(&_mtx);

  _notify_event_handlers(WRC_EVT_WORKER_SHUTDOWN, w->id, 0, 0);
  _worker_unref(w);
}

static iwrc _fd_make_non_blocking(int fd) {
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

static void _on_stdout(const struct iwn_proc_ctx *ctx, const char *buf, size_t len) {
  if (g_env.log.verbose) {
    iwlog_info("WRC[%d]: %.*s", ctx->pid, (int) len, buf);
  }
}

static void _on_stderr(const struct iwn_proc_ctx *ctx, const char *buf, size_t len) {
  iwlog_info("WRC[%d]: %.*s", ctx->pid, (int) len, buf);
}

static iwrc _worker_fds_init(struct _worker *w) {
  iwrc rc = 0;
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); i += 2) {
    RCN(finish, pipe(&w->fds[i]));
  }
  RCC(rc, finish, _fd_make_non_blocking(w->fds[FD_MSG_IN]));
  RCC(rc, finish, _fd_make_non_blocking(w->fds[FD_MSG_OUT]));
  RCC(rc, finish, _fd_make_non_blocking(w->fds[FD_PAYLOAD_OUT]));
  RCC(rc, finish, _fd_make_non_blocking(w->fds[FD_PAYLOAD_IN]));

finish:
  return rc;
}

static void _on_worker_fd_close(const struct iwn_poller_task *t) {
  struct _worker *w = t->user_data;
  assert(w);
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); ++i) {
    if (w->fds[i] == t->fd) {
      w->fds[i] = -1;
      break;
    }
  }
  iwn_proc_unref(w->id);
}

static void _on_fork_parent(const struct iwn_proc_ctx *ctx, pid_t pid) {
  iwrc rc = 0;
  struct _worker *w = ctx->user_data;
  assert(w);
  w->id = pid;

  _worker_close_fd(w, FD_MSG_IN_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_MSG_IN],
    .user_data = w,
    .on_ready = _on_msg_read,
    .on_dispose = _on_worker_fd_close,
    .events = IWN_POLLIN,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

  _worker_close_fd(w, FD_PAYLOAD_IN_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_PAYLOAD_IN],
    .user_data = w,
    .on_ready = _on_payload_read,
    .on_dispose = _on_worker_fd_close,
    .events = IWN_POLLIN,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

  _worker_close_fd(w, FD_MSG_OUT_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_MSG_OUT],
    .user_data = w,
    .on_ready = _on_msg_write,
    .on_dispose = _on_worker_fd_close,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

  _worker_close_fd(w, FD_PAYLOAD_OUT_C);
  RCC(rc, finish, iwn_poller_add(&(struct iwn_poller_task) {
    .poller = g_env.poller,
    .fd = w->fds[FD_PAYLOAD_OUT],
    .user_data = w,
    .on_ready = _on_payload_write,
    .on_dispose = _on_worker_fd_close,
    .events_mod = IWN_POLLET,
  }));
  iwn_proc_ref(pid);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    _worker_kill_lk(w);
  }
}

static void _on_fork_child(const struct iwn_proc_ctx *ctx) {
  struct _worker *w = ctx->user_data;
  assert(w);
  while ((dup2(w->fds[FD_MSG_OUT_C], 3) == -1) && (errno == EINTR));
  while ((dup2(w->fds[FD_MSG_IN_C], 4) == -1) && (errno == EINTR));
  while ((dup2(w->fds[FD_PAYLOAD_OUT_C], 5) == -1) && (errno == EINTR));
  while ((dup2(w->fds[FD_PAYLOAD_IN_C], 6) == -1) && (errno == EINTR));
  _worker_close_fds(w);
}

static void _on_fork(const struct iwn_proc_ctx *ctx, pid_t pid) {
  if (pid > 0) {
    _on_fork_parent(ctx, pid);
  } else {
    _on_fork_child(ctx);
  }
}

static iwrc _worker_spawn(wrc_resource_t *id) {
  *id = -1;
  iwrc rc = 0;
  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  struct _worker *w = iwpool_calloc(sizeof(*w), pool);
  if (!w) {
    iwpool_destroy(pool);
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  w->pool = pool;
  w->refs = 1;
  pthread_mutex_init(&w->mtx, 0);
  w->readlen_msg = -1;
  w->readlen_payload = -1;
  for (int i = 0; i < sizeof(w->fds) / sizeof(w->fds[0]); ++i) {
    w->fds[i] = -1;
  }

  struct iwn_proc_spec spec = (struct iwn_proc_spec) {
    .poller = g_env.poller,
    .path = g_env.worker.program ? g_env.worker.program : g_env.program_file,
    .user_data = w,
    .on_stdout = _on_stdout,
    .on_stderr = _on_stderr,
    .on_exit = _on_exit,
    .on_fork = _on_fork
  };

  IWXSTR *xargs = iwxstr_new();
  RCB(finish, xargs);
  RCB(finish, w->readbuf_msg = iwxstr_new());
  RCB(finish, w->readbuf_payload = iwxstr_new());

  if (spec.path == g_env.program_file) {
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
  RCB(finish, spec.args = iwpool_split_string(pool, iwxstr_ptr(xargs), "\1", true));
  if (spec.path != g_env.program_file) {
    spec.env = iwpool_split_string(pool, "MEDIASOUP_VERSION=3.9.9", ",", true);
  }
  if (g_env.log.verbose) {
    char *cmd = iwn_proc_command_get(&spec);
    if (cmd) {
      iwlog_info("WRC Worker spawn: %s", cmd);
    }
    free(cmd);
  }

  RCC(rc, finish, _worker_fds_init(w));
  rc = iwn_proc_spawn(&spec, &w->id);
  if (!rc) {
    *id = w->id;
  }

  pthread_mutex_lock(&_mtx);
  _worker_register_lk(w);
  pthread_mutex_unlock(&_mtx);

finish:
  if (rc) {
    _worker_destroy_lk(w);
  }
  iwxstr_destroy(xargs);
  return rc;
}

void wrc_shutdown(void) {
  _shutdown_pending = true;
  pthread_mutex_lock(&_mtx);
  for (struct _worker *w = _workers; w; w = w->next) {
    _worker_kill_lk(w);
  }
  pthread_mutex_unlock(&_mtx);
}

iwrc wrc_notify_event_handlers(wrc_event_e evt, wrc_resource_t resource_id, JBL data) {
  return _notify_event_handlers(evt, resource_id, data, 0);
}

iwrc wrc_remove_event_handler(wrc_event_handler_t id) {
  iwrc rc = 0;
  pthread_mutex_lock(&_mtx);
  IWULIST *list = &_listeners;
  for (size_t i = 0, l = iwulist_length(list); i < l; ++i) {
    struct _event_lsnr *n = iwulist_at2(list, i);
    if (n->id == id) {
      rc = iwulist_remove(list, i);
      break;
    }
  }
  pthread_mutex_unlock(&_mtx);
  return rc;
}

iwrc wrc_add_event_handler(wrc_event_handler event_handler, void *op, wrc_event_handler_t *oid) {
  iwrc rc = 0;
  pthread_mutex_lock(&_mtx);
  IWULIST *list = &_listeners;
  wrc_event_handler_t id = 0;
  for (size_t i = 0, l = iwulist_length(list); i < l; ++i) {
    struct _event_lsnr *n = iwulist_at2(list, i);
    if (n->id > id) {
      id = n->id;
    }
  }
  while (!++id);
  rc = iwulist_push(list, &(struct _event_lsnr) {
    .id = id,
    .h = event_handler,
    .op = op
  });
  pthread_mutex_unlock(&_mtx);
  if (!rc) {
    *oid = id;
  }
  return rc;
}

void wrc_register_uuid_resolver(wrc_resource_t (*resolver)(const char *uuid)) {
  _uuid_resolver = resolver;
}

void wrc_ajust_load_score(wrc_resource_t id, int load_score) {
  int score_old = 0, score_new = 0;
  pthread_mutex_lock(&_mtx);
  struct _worker *w = _worker_find_lk(id);
  if (w) {
    score_old = w->load_score;
    w->load_score += load_score;
    score_new = w->load_score;
    if (w->load_score < 1) {
      iwp_current_time_ms(&w->load_score_zerotime, true);
    }
  }
  pthread_mutex_unlock(&_mtx);
  if (g_env.log.verbose && w) {
    iwlog_info("Adjust worker load score: %" PRIu32 " %d => %d", id, score_old, score_new);
  }
}

static wrc_resource_t _worker_acquire(uint64_t ts) {
  int score = INT_MAX, cnt = 0;
  wrc_resource_t ret = -1;

  pthread_mutex_lock(&_mtx);
  for (struct _worker *w = _workers; w; w = w->next) {
    if (!w->shutdown) {
      ++cnt;
      if (w->load_score < score) {
        score = w->load_score;
        ret = w->id;
      }
    }
  }
  pthread_mutex_unlock(&_mtx);

  if (ret == -1 || (cnt < g_env.worker.max_workers && score > 0)) {
    wrc_resource_t id;
    iwrc rc = _worker_spawn(&id);
    if (rc) {
      iwlog_ecode_error3(rc);
      return ret;
    } else {
      ret = id;
    }
  } else {
    pthread_mutex_lock(&_mtx);
    for (struct _worker *w = _workers; w; w = w->next) {
      if (  w->id != ret
         && !w->shutdown
         && w->load_score < 1
         && w->load_score_zerotime > 0
         && ts - w->load_score_zerotime >= (uint64_t) g_env.worker.idle_timeout_sec * 1000) {
        iwlog_info("Shutting down idle worker: %" PRIu32, w->id);
        _worker_kill_lk(w);
      }
    }
    pthread_mutex_unlock(&_mtx);
  }

  return ret;
}

static iwrc _worker_send_payload_wlk(struct _worker *w, struct _msg *m) {
  iwrc rc = 0;
  JBL jbl = 0;
  uint32_t len, lv;

  const char *event;
  struct wrc_msg *mm = &m->mm;
  IWXSTR *xstr = w->writebuf_payload;
  if (!xstr) {
    RCB(finish, xstr = w->writebuf_payload = iwxstr_new());
  }
  struct wrc_payload_input *pli = &mm->input.payload;
  const char *payload = pli->const_payload ? pli->const_payload : pli->payload;
  if (!payload || !pli->internal) {
    return IW_ERROR_INVALID_ARGS;
  }

  switch (pli->type) {
    case WRC_PAYLOAD_PRODUCER_SEND:
      event = "dataProducer.send";
      break;
    case WRC_PAYLOAD_RTP_PACKET_SEND:
      event = "producer.send";
      break;
    case WRC_PAYLOAD_RTCP_PACKET_SEND:
      event = "transport.sendRtcp";
      break;
    default:
      return IW_ERROR_INVALID_ARGS;
  }

  // First part
  len = iwxstr_size(xstr);
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, iwxstr_cat(xstr, &len, 4));
  RCC(rc, finish, jbl_set_string(jbl, "event", event));
  RCC(rc, finish, jbl_set_nested(jbl, "internal", pli->internal));
  if (pli->data) {
    RCC(rc, finish, jbl_set_nested(jbl, "data", pli->data));
  }
  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));

  lv = iwxstr_size(xstr) - len - 4;
  lv = IW_HTOIL(lv);
  memcpy(iwxstr_ptr(xstr) + len, &lv, 4);

  // Second part
  lv = pli->payload_len;
  lv = IW_HTOIL(lv);
  RCC(rc, finish, iwxstr_cat(xstr, &lv, 4));
  RCC(rc, finish, iwxstr_cat(xstr, payload, pli->payload_len));
  RCC(rc, finish, iwn_poller_arm_events(g_env.poller, w->fds[FD_PAYLOAD_OUT], IWN_POLLOUT));

finish:
  _msg_destroy(m);
  if (rc) {
    iwxstr_pop(xstr, iwxstr_size(xstr) - len);
  }
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _worker_send_wlk(struct _worker *w, struct _msg *m) {
  iwrc rc = 0;
  uint32_t len, lv;
  struct wrc_msg *mm = &m->mm;
  const char *method = _worker_cmd_name(mm->input.worker.cmd);
  if (!method) {
    iwlog_warn("WRC Unknown worker command %d", mm->input.worker.cmd);
    return IW_ERROR_INVALID_ARGS;
  }
  JBL jbl = 0;
  IWXSTR *xstr = w->writebuf_msg;
  if (!xstr) {
    RCB(finish, xstr = w->writebuf_msg = iwxstr_new());
  }
  struct wrc_worker_input *in = &mm->input.worker;
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_int64(jbl, "id", m->id));
  RCC(rc, finish, jbl_set_string(jbl, "method", method));
  if (in->internal) {
    RCC(rc, finish, jbl_set_nested(jbl, "internal", in->internal));
  }
  if (in->data) {
    RCC(rc, finish, jbl_set_nested(jbl, "data", in->data));
  }

  len = iwxstr_size(xstr);
  RCC(rc, finish, iwxstr_cat(xstr, &len, 4));
  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));
  if (g_env.log.verbose) {
    iwlog_info("WRC[%d] send command: %s", w->id, iwxstr_ptr(xstr) + 4);
  }
  lv = iwxstr_size(xstr) - len - 4;
  lv = IW_HTOIL(lv);
  memcpy(iwxstr_ptr(xstr) + len, &lv, 4);

  RCC(rc, finish, iwn_poller_arm_events(g_env.poller, w->fds[FD_MSG_OUT], IWN_POLLOUT));

  if (mm->handler) {
    pthread_mutex_lock(&_mtx);
    if (_reply_tail == 0) {
      _reply = _reply_tail = m;
    } else {
      _reply_tail->next_reply = m;
    }
    pthread_mutex_unlock(&_mtx);
  }

finish:
  if (rc) {
    _msg_destroy(m);
    iwxstr_pop(xstr, iwxstr_size(xstr) - len);
  } else if (!mm->handler) {
    _msg_destroy(m);
  }
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _worker_send(struct _msg *m) {
  iwrc rc = 0;
  struct wrc_msg *mm = &m->mm;
  struct _worker *w = _worker_find_ref(mm->resource_id);

  if (!w) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    iwlog_ecode_warn(rc, "WRC worker:%d not found or worker in shutdown pending state", mm->resource_id);
    goto finish;
  }

  pthread_mutex_lock(&w->mtx);
  if (IW_UNLIKELY(mm->type == WRC_MSG_PAYLOAD)) {
    rc = _worker_send_payload_wlk(w, m);
  } else {
    rc = _worker_send_wlk(w, m);
  }
  pthread_mutex_unlock(&w->mtx);

finish:
  if (w) {
    _worker_unref(w);
  }
  return rc;
}

static void _msg_init(struct _msg *m) {
  static uint32_t seq = 0;
  iwp_current_time_ms(&m->ts, true);
  pthread_mutex_init(&m->cond_mtx, 0);
  pthread_cond_init(&m->cond_completed, 0);
  pthread_mutex_lock(&_mtx);
  do {
    m->id = ++seq;
  } while (m->id == 0);
  pthread_mutex_unlock(&_mtx);
}

static iwrc _send(struct _msg *m) {
  if (_shutdown_pending) {
    return GR_ERROR_WORKER_EXIT;
  }
  iwrc rc;
  switch (m->mm.type) {
    case WRC_MSG_WORKER:
    case WRC_MSG_PAYLOAD:
      rc = _worker_send(m);
      if (rc) {
        m->mm.rc = rc;
        WRC_MSG_COMPLETE_HANDLER(m);
      }
      break;
    case WRC_MSG_EVENT: {
      struct wrc_event_input *e = &m->mm.input.event;
      rc = _notify_event_handlers(e->evt, e->resource_id, e->data, m);
      break;
    }
    default:
      rc = IW_ERROR_INVALID_ARGS;
      m->mm.rc = rc;
      WRC_MSG_COMPLETE_HANDLER(m);
      break;
  }
  return rc;
}

wrc_msg_t* wrc_msg_create(const wrc_msg_t *proto) {
  struct _msg *m = calloc(1, sizeof(*m));
  if (!m) {
    return 0;
  }
  if (proto) {
    memcpy(&m->mm, proto, sizeof(*proto));
  }
  _msg_init(m);
  return (void*) m;
}

iwrc wrc_send(struct wrc_msg *m_) {
  return _send((void*) m_);
}

static void _send_and_wait_handler(struct wrc_msg *m_) {
  struct _msg *m = (void*) m_;
  pthread_mutex_lock(&m->cond_mtx);
  pthread_cond_broadcast(&m->cond_completed);
  pthread_mutex_unlock(&m->cond_mtx);
}

iwrc wrc_send_and_wait(wrc_msg_t *m_, int timeout_sec) {
  int rci;
  struct _msg *m = (void*) m_;
  m->mm.handler = _send_and_wait_handler;
  iwrc rc = _send(m);
  RCRET(rc);

  if (timeout_sec == 0) {
    timeout_sec = g_env.worker.command_timeout_sec;
  } else if (timeout_sec == -1) {
    timeout_sec = 0;
  }

  pthread_mutex_lock(&m->cond_mtx);
  if (timeout_sec > 0) {
    struct timespec tp;
    RCC(rc, finish, iwp_clock_get_time(CLOCK_MONOTONIC, &tp));
    tp.tv_sec += timeout_sec;
    do {
      rci = pthread_cond_timedwait(&m->cond_completed, &m->cond_mtx, &tp);
    } while (rci == -1 && errno == EINTR);
  } else {
    do {
      rci = pthread_cond_wait(&m->cond_completed, &m->cond_mtx);
    } while (rci == -1 && errno == EINTR);
  }
  RCN(finish, rci);

  if (!rc && m->mm.rc) {
    rc = m->mm.rc;
  }

finish:
  pthread_mutex_unlock(&m->cond_mtx);
  if (rc) {
    pthread_mutex_lock(&_mtx);
    for (struct _msg *rm = _reply, *p = 0; rm; p = rm, rm = rm->next_reply) {
      if (rm == m) {
        if (p) {
          p->next_reply = rm->next_reply;
          if (rm->next_reply == 0) {
            _reply_tail = p;
          }
        } else {
          _reply = rm->next_reply;
          if (rm->next_reply == 0) {
            _reply_tail = 0;
          }
        }
        break;
      }
    }
    pthread_mutex_unlock(&_mtx);
  }
  return rc;
}

void wrc_worker_kill(wrc_resource_t id) {
  pthread_mutex_lock(&_mtx);
  struct _worker *w = _worker_find_lk(id);
  if (w) {
    _worker_kill_lk(w);
  }
  pthread_mutex_unlock(&_mtx);
}

iwrc wrc_worker_acquire(wrc_resource_t *out_id) {
  uint64_t ts = 0;
  iwp_current_time_ms(&ts, true);
  *out_id = _worker_acquire(ts);
  if (*out_id == -1) {
    return IW_ERROR_FAIL;
  }
  return 0;
}

iwrc wrc_init(void) {
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&_mtx, &mattr);
  pthread_mutexattr_destroy(&mattr);
  RCR(iwstw_start("wrc_stw", 10000, false, &_stw));
  if (!_listeners.usize) {
    iwulist_init(&_listeners, 0, sizeof(struct _event_lsnr));
  }
  _stalled_messages_cleanup_worker(0);
  return 0;
}

void wrc_close(void) {
  iwstw_shutdown(&_stw, true);
  for (struct _msg *m = _reply, *next; m; m = next) {
    next = m->next_reply;
    _msg_destroy(m);
  }
  _reply = _reply_tail = 0;
  iwulist_destroy_keep(&_listeners);
  pthread_mutex_destroy(&_mtx);
}
