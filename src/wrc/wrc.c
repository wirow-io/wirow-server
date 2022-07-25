#include "wrc.h"
#include "wrc_adapter.h"

#include <iowow/iwarr.h>
#include <iowow/iwstw.h>
#include <iwnet/iwn_scheduler.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

extern struct gr_env g_env;

#define WRC_MSG_COMPLETE_HANDLER(msg_)           \
  if ((msg_)->mm.handler) {                      \
    (msg_)->mm.handler((void*) msg_);            \
  } else {                                       \
    _msg_destroy(msg_);                          \
  }

struct event_lsnr {
  wrc_event_handler_t id;
  wrc_event_handler   h;
  void *op;
};

struct event_work {
  wrc_event_e    evt;
  wrc_resource_t resource_id;
  JBL data;
  struct msg *m;
};

struct msg {
  struct wrc_msg  mm;
  struct msg     *next_reply;
  pthread_mutex_t cond_mtx;
  pthread_cond_t  cond_completed;
  uint64_t ts;
  uint32_t id;
  bool     in_wait;
};

struct wa { // Worker adapter
  wrc_resource_t id;
  JBL ongoing_payload_notification;
};

static pthread_mutex_t _mtx = PTHREAD_MUTEX_INITIALIZER;
static wrc_resource_t _wid;
static IWULIST _listeners;
static IWSTW _stw;
static struct msg *_reply, *_reply_tail;
static atomic_bool _shutdown_pending;

wrc_resource_t (*_uuid_resolver)(const char *uuid);

void wrc_register_uuid_resolver(wrc_resource_t (*resolver)(const char *uuid)) {
  _uuid_resolver = resolver;
}

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

static void _worker_destroy(struct wa *w) {
  if (w) {
    jbl_destroy(&w->ongoing_payload_notification);
    free(w);
  }
}

static void _msg_destroy(struct msg *m) {
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

iwrc wrc_remove_event_handler(wrc_event_handler_t id) {
  iwrc rc = 0;
  pthread_mutex_lock(&_mtx);
  IWULIST *list = &_listeners;
  for (size_t i = 0, l = iwulist_length(list); i < l; ++i) {
    struct event_lsnr *n = iwulist_at2(list, i);
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
    struct event_lsnr *n = iwulist_at2(list, i);
    if (n->id > id) {
      id = n->id;
    }
  }
  while (!++id);
  rc = iwulist_push(list, &(struct event_lsnr) {
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

static void _notify_event_handlers_worker(void *arg) {
  IWULIST *list;
  struct event_work *ew = arg;

  pthread_mutex_lock(&_mtx);
  list = iwulist_clone(&_listeners);
  pthread_mutex_unlock(&_mtx);

  for (size_t i = 0, l = iwulist_length(list); i < l; ++i) {
    struct event_lsnr *n = iwulist_at2(list, i);
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

static iwrc _notify_event_handlers(wrc_event_e evt, wrc_resource_t resource_id, JBL data, struct msg *m) {
  iwrc rc = 0;
  struct event_work *ew;

  RCB(finish, ew = malloc(sizeof(*ew)));
  *ew = (struct event_work) {
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

iwrc wrc_notify_event_handlers(wrc_event_e evt, wrc_resource_t resource_id, JBL data) {
  return _notify_event_handlers(evt, resource_id, data, 0);
}

static void _on_closed(wrc_resource_t wid, void *user_data, const char *note) {
  iwlog_info("WRC[0x%" PRIx64 "] exited %s", wid, note ? note : "");
  struct wa *w = user_data;
  pthread_mutex_lock(&_mtx);
  _wid = 0;
  for (struct msg *m = _reply; m; m = m->next_reply) {
    if (m->mm.worker_id == wid) {
      m->mm.rc = GR_ERROR_WORKER_EXIT;
      pthread_mutex_lock(&m->cond_mtx);
      pthread_cond_broadcast(&m->cond_completed);
      pthread_mutex_unlock(&m->cond_mtx);
    }
  }
  pthread_mutex_unlock(&_mtx);
  _worker_destroy(w);
  _notify_event_handlers(WRC_EVT_WORKER_SHUTDOWN, wid, 0, 0);
}

static void _on_msg(wrc_resource_t wid, const char *cmd, size_t cmd_len, void *user_data) {
  iwrc rc = 0;
  JBL jbl = 0;
  int64_t id = 0;
  struct msg *hm = 0;
  struct wa *w = user_data;
  bool data_with_event = false;

  switch (*cmd) {
    case 'D':
      iwlog_info("WRC<0x%lx> %s", w->id, cmd + 1);
      break;
    case 'W':
      iwlog_warn("WRC<0x%lx> %s", w->id, cmd + 1);
      break;
    case 'E':
      iwlog_error("WRC<0x%lx> %s", w->id, cmd + 1);
      break;
    case 'X':
      iwlog_info("WRC<0x%lx> %s", w->id, cmd + 1);
      break;
    case '{':
      if (g_env.log.verbose) {
        iwlog_info("WRC<0x%lx> %s\n", w->id, cmd);
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
      for (struct msg *m = _reply, *p = 0; m; p = m, m = m->next_reply) {
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

static void _on_payload(wrc_resource_t wid, const char *buf, size_t len, void *user_data) {
  iwrc rc = 0;
  JBL jbl_notify = 0;
  struct wa *w = user_data;

  pthread_mutex_lock(&_mtx);
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
  pthread_mutex_unlock(&_mtx);

  if (jbl_notify) {
    _notify_event_handlers(WRC_EVT_PAYLOAD, w->id, jbl_notify, 0);
  }
}

static void _msg_init(struct msg *m) {
  static atomic_uint seq = 0;
  iwp_current_time_ms(&m->ts, true);
  pthread_mutex_init(&m->cond_mtx, 0);
  pthread_cond_init(&m->cond_completed, 0);
  do {
    m->id = ++seq;
  } while (m->id == 0);
}

wrc_msg_t* wrc_msg_create(const wrc_msg_t *proto) {
  struct msg *m = calloc(1, sizeof(*m));
  if (!m) {
    return 0;
  }
  if (proto) {
    memcpy(&m->mm, proto, sizeof(*proto));
  }
  _msg_init(m);
  return (void*) m;
}

static iwrc _worker_send_msg(struct msg *m) {
  iwrc rc = 0;
  uint32_t len = 0, lv;
  struct wrc_msg *mm = &m->mm;
  const char *method = _worker_cmd_name(mm->input.worker.cmd);
  if (!method) {
    iwlog_warn("WRC Unknown worker command %d", mm->input.worker.cmd);
    return IW_ERROR_INVALID_ARGS;
  }
  JBL jbl = 0;
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
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

  RCC(rc, finish, iwxstr_cat(xstr, &len, 4));
  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));
  if (g_env.log.verbose) {
    iwlog_info("WRC[0x%" PRIx64 "] send command: %s", mm->worker_id, iwxstr_ptr(xstr) + 4);
  }
  lv = iwxstr_size(xstr) - 4;
  lv = IW_HTOIL(lv);
  memcpy(iwxstr_ptr(xstr), &lv, 4);

  if (mm->handler) {
    pthread_mutex_lock(&_mtx);
    if (_reply_tail == 0) {
      _reply = _reply_tail = m;
    } else {
      _reply_tail->next_reply = m;
    }
    pthread_mutex_unlock(&_mtx);
  }

  rc = wrc_adapter_send_data(mm->worker_id, iwxstr_ptr(xstr), iwxstr_size(xstr));

  if (rc && mm->handler) {
    pthread_mutex_lock(&_mtx);
    for (struct msg *m_ = _reply, *p = 0; m_; p = m_, m_ = m_->next_reply) {
      if (m->id == m_->id) {
        if (p) {
          p->next_reply = m_->next_reply;
          if (m_->next_reply == 0) {
            _reply_tail = p;
          }
        } else {
          _reply = m_->next_reply;
          if (m_->next_reply == 0) {
            _reply_tail = 0;
          }
        }
        break;
      }
    }
    pthread_mutex_unlock(&_mtx);
  }

finish:
  if (rc || !m->mm.handler) {
    m->mm.rc = rc;
    WRC_MSG_COMPLETE_HANDLER(m);
  }
  jbl_destroy(&jbl);
  iwxstr_destroy(xstr);
  return rc;
}

static iwrc _worker_send_payload(struct msg *m) {
  iwrc rc = 0;
  JBL jbl = 0;
  uint32_t len, lv;

  const char *event;
  struct wrc_msg *mm = &m->mm;
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

  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  // First part
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, iwxstr_cat(xstr, &len, 4));
  RCC(rc, finish, jbl_set_string(jbl, "event", event));
  RCC(rc, finish, jbl_set_nested(jbl, "internal", pli->internal));
  if (pli->data) {
    RCC(rc, finish, jbl_set_nested(jbl, "data", pli->data));
  }
  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));

  lv = iwxstr_size(xstr) - 4;
  lv = IW_HTOIL(lv);
  memcpy(iwxstr_ptr(xstr), &lv, 4);

  // Second part
  lv = pli->payload_len;
  lv = IW_HTOIL(lv);
  RCC(rc, finish, iwxstr_cat(xstr, &lv, 4));
  RCC(rc, finish, iwxstr_cat(xstr, payload, pli->payload_len));

  // Send payload
  rc = wrc_adapter_send_payload(mm->worker_id, iwxstr_ptr(xstr), iwxstr_size(xstr));

finish:
  _msg_destroy(m);
  jbl_destroy(&jbl);
  iwxstr_destroy(xstr);
  return rc;
}

static iwrc _worker_send(struct msg *m) {
  iwrc rc = 0;
  struct wrc_msg *mm = &m->mm;
  if (IW_UNLIKELY(mm->type == WRC_MSG_PAYLOAD)) {
    rc = _worker_send_payload(m);
  } else {
    rc = _worker_send_msg(m);
  }
  return rc;
}

static iwrc _send(struct msg *m) {
  if (_shutdown_pending) {
    return GR_ERROR_WORKER_EXIT;
  }
  iwrc rc;
  switch (m->mm.type) {
    case WRC_MSG_WORKER:
    case WRC_MSG_PAYLOAD:
      rc = _worker_send(m);
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

iwrc wrc_send(wrc_msg_t *m) {
  return _send((void*) m);
}

static void _send_and_wait_handler(struct wrc_msg *m_) {
  struct msg *m = (void*) m_;
  pthread_mutex_lock(&m->cond_mtx);
  if (m->in_wait) {
    pthread_cond_broadcast(&m->cond_completed);
  } else {
    m->in_wait = true;
  }
  pthread_mutex_unlock(&m->cond_mtx);
}

iwrc wrc_send_and_wait(wrc_msg_t *m_, int timeout_sec) {
  int rci;
  struct msg *m = (void*) m_;
  m->mm.handler = _send_and_wait_handler;

  iwrc rc = _send(m);
  RCRET(rc);

  if (timeout_sec == 0) {
    timeout_sec = g_env.worker.command_timeout_sec;
  } else if (timeout_sec == -1) {
    timeout_sec = 0;
  }

  pthread_mutex_lock(&m->cond_mtx);
  if (!m->in_wait) {
    m->in_wait = true;
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
  }

  if (!rc && m->mm.rc) {
    rc = m->mm.rc;
  }

finish:
  pthread_mutex_unlock(&m->cond_mtx);
  pthread_mutex_lock(&_mtx);
  for (struct msg *m_ = _reply, *p = 0; m_; p = m_, m_ = m_->next_reply) {
    if (m->id == m_->id) {
      if (p) {
        p->next_reply = m_->next_reply;
        if (m_->next_reply == 0) {
          _reply_tail = p;
        }
      } else {
        _reply = m_->next_reply;
        if (m_->next_reply == 0) {
          _reply_tail = 0;
        }
      }
      break;
    }
  }
  pthread_mutex_unlock(&_mtx);
  return rc;
}

static iwrc _worker_acquire_lk(wrc_resource_t *out_id) {
  struct wa *w = calloc(1, sizeof(*w));
  if (!w) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  struct wrc_adapter_spec spec = {
    .user_data  = w,
    .on_msg     = _on_msg,
    .on_payload = _on_payload,
    .on_closed  = _on_closed
  };
  iwrc rc = wrc_adapter_create(&spec, out_id);
  if (rc) {
    _worker_destroy(w);
  } else {
    w->id = *out_id;
  }
  return rc;
}

iwrc wrc_worker_acquire(wrc_resource_t *out_wid) {
  pthread_mutex_lock(&_mtx);
  if (_wid) {
    *out_wid = _wid;
    pthread_mutex_unlock(&_mtx);
    return 0;
  }
  iwrc rc = _worker_acquire_lk(out_wid);
  if (!rc) {
    _wid = *out_wid;
  }
  pthread_mutex_unlock(&_mtx);
  return rc;
}

void wrc_worker_kill(wrc_resource_t wid) {
  wrc_adapter_close(wid);
}

void wrc_ajust_load_score(wrc_resource_t wid, int load_score) {
  // NOOP
  // TODO:
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
  for (struct msg *m = _reply, *p = 0; m; m = m->next_reply) {
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
    struct msg *m = *(struct msg**) iwulist_at2(&list, i);
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

iwrc wrc_init(void) {
  iwrc rc = wrc_adapter_module_init();
  if (rc) {
    return rc;
  }
  RCR(iwstw_start("wrc_stw", 10000, false, &_stw));
  if (!_listeners.usize) {
    iwulist_init(&_listeners, 0, sizeof(struct event_lsnr));
  }
  return rc;
}

void wrc_shutdown(void) {
  _shutdown_pending = true;
  _wid = 0;
  wrc_adapter_module_shutdown();
  iwstw_shutdown(&_stw, true);
}

void wrc_destroy(void) {
  for (struct msg *m = _reply, *next; m; m = next) {
    next = m->next_reply;
    _msg_destroy(m);
  }
  _wid = 0;
  _reply = _reply_tail = 0;
  wrc_adapter_module_destroy();
  iwulist_destroy_keep(&_listeners);
}

IW_DESTRUCTOR static void _destroy(void) {
  pthread_mutex_destroy(&_mtx);
}
