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

#include "rct.h"
#include "rct_h264.h"
#include "rct_modules.h"
#include "gr_gauges.h"

#include <iowow/iwconv.h>
#include <iowow/iwre.h>

#include <string.h>
#include <assert.h>

#include "data_supported_rtp_capabilities.inc"

struct rct_state state = {
  .mtx = PTHREAD_MUTEX_INITIALIZER
};

IW_INLINE void _lock(void) {
  pthread_mutex_lock(&state.mtx);
}

IW_INLINE void _unlock(void) {
  pthread_mutex_unlock(&state.mtx);
}

void rct_lock(void) {
  _lock();
}

void rct_unlock(void) {
  _unlock();
}

static void _resource_gauge_update_lk(rct_resource_base_t *b) {
  uint32_t gauge = 0;
  int32_t gtype = b->type;
  switch (b->type) {
    case RCT_TYPE_ROOM:
      gauge = GAUGE_ROOMS;
      break;
    case RCT_TYPE_ROOM_MEMBER:
      gauge = GAUGE_ROOM_USERS;
      break;
    case RCT_TYPE_CONSUMER:
    case RCT_TYPE_PRODUCER:
      gauge = GAUGE_STREAMS;
      gtype = RCT_TYPE_PRODUCER | RCT_TYPE_CONSUMER;
      break;
  }
  if (gauge) {
    int num = rct_resource_get_number_of_type_lk(gtype);
    if (num >= 0) {
      gr_gauge_set_async(gauge, num);
    }
  }
}

static void _resource_unregister_lk(wrc_resource_t resource_id) {
  rct_resource_base_t *b = iwhmap_get_u64(state.map_id2ptr, resource_id);
  if (b) {
    iwhmap_remove_u64(state.map_id2ptr, resource_id);
    iwhmap_remove(state.map_uuid2ptr, b->uuid);
    if (b->wid) {
      wrc_ajust_load_score(b->wid, -1);
    }
    _resource_gauge_update_lk(b);
  }
}

static void _resource_dispose_lk(rct_resource_base_t *b) {
  assert(b->refs < 1);
  iwlog_debug("RCT Dispose [%s]: 0x%" PRIx64, rct_resource_type_name(b->type), b->id);

  switch (b->type) {
    case RCT_TYPE_PRODUCER:
    case RCT_TYPE_PRODUCER_DATA:
      rct_producer_dispose_lk((void*) b);
      break;
    case RCT_TYPE_CONSUMER:
    case RCT_TYPE_CONSUMER_DATA:
      rct_consumer_dispose_lk((void*) b);
      break;
    case RCT_TYPE_TRANSPORT_WEBRTC:
    case RCT_TYPE_TRANSPORT_PLAIN:
    case RCT_TYPE_TRANSPORT_DIRECT:
    case RCT_TYPE_TRANSPORT_PIPE:
      rct_transport_dispose_lk((void*) b);
      break;
    default:
      if (b->dispose) {
        b->dispose(b);
      }
      break;
  }
  if (b->user_data) {
    free(b->user_data);
    b->user_data = 0;
  }
  _resource_unregister_lk(b->id);
  iwpool_destroy(b->pool);
}

void* rct_resource_close_lk(void *v) {
  rct_resource_base_t *b = v;
  if (b->closed) {
    return b;
  }
  b->closed = true;

  iwlog_debug("RCT Close [%s]: 0x%" PRIx64, rct_resource_type_name(b->type), b->id);
  switch (b->type) {
    case RCT_TYPE_ROUTER:
      rct_router_close_lk((void*) b);
      break;
    case RCT_TYPE_PRODUCER:
    case RCT_TYPE_PRODUCER_DATA:
      rct_producer_close_lk((void*) b);
      break;
    case RCT_TYPE_TRANSPORT_WEBRTC:
    case RCT_TYPE_TRANSPORT_PLAIN:
    case RCT_TYPE_TRANSPORT_DIRECT:
    case RCT_TYPE_TRANSPORT_PIPE:
      rct_transport_close_lk((void*) b);
      break;
    default:
      if (b->close) {
        b->close(b);
      }
      break;
  }
  // Do release the main ref
  return rct_resource_ref_lk(b, -1, __func__);
}

static void _resource_close(wrc_resource_t resource_id) {
  rct_resource_base_t *b = rct_resource_by_id_locked(resource_id, 0, __func__);
  if (b) {
    b = rct_resource_close_lk(b);
  }
  rct_resource_unlock(b, __func__);
}

void rct_resource_close(wrc_resource_t resource_id) {
  _resource_close(resource_id);
}

void rct_resource_close_of_type(uint32_t resource_type) {
  IWULIST list;
  IWHMAP_ITER iter;
  iwhmap_iter_init(state.map_id2ptr, &iter);
  iwulist_init(&list, 32, sizeof(wrc_resource_t));
  _lock();
  while (iwhmap_iter_next(&iter)) {
    const rct_resource_base_t *b = iter.val;
    if (b && b->type == resource_type) {
      iwulist_push(&list, &b->id);
    }
  }
  _unlock();
  for (size_t i = 0, l = iwulist_length(&list); i < l; ++i) {
    wrc_resource_t id = *(wrc_resource_t*) iwulist_at2(&list, i);
    rct_resource_close(id);
  }
  iwulist_destroy_keep(&list);
}

const char* rct_resource_type_name(int type) {
  switch (type) {
    case RCT_TYPE_ROUTER:
      return "ROUTER              ";
    case RCT_TYPE_TRANSPORT_WEBRTC:
      return "TRANSPORT_WEBRTC    ";
    case RCT_TYPE_TRANSPORT_PLAIN:
      return "TRANSPORT_PLAIN     ";
    case RCT_TYPE_TRANSPORT_DIRECT:
      return "TRANSPORT_DIRECT    ";
    case RCT_TYPE_TRANSPORT_PIPE:
      return "TRANSPORT_PIPE      ";
    case RCT_TYPE_PRODUCER:
      return "PRODUCER            ";
    case RCT_TYPE_PRODUCER_DATA:
      return "PRODUCER_DATA       ";
    case RCT_TYPE_CONSUMER:
      return "CONSUMER            ";
    case RCT_TYPE_CONSUMER_DATA:
      return "CONSUMER_DATA       ";
    case RCT_TYPE_ROOM:
      return "ROOM                ";
    case RCT_TYPE_ROOM_MEMBER:
      return "ROOM_MEMBER         ";
    case RCT_TYPE_OBSERVER_AL:
      return "RCT_TYPE_OBSERVER_AL";
    case RCT_TYPE_OBSERVER_AS:
      return "RCT_TYPE_OBSERVER_AS";
    case RCT_TYPE_PRODUCER_EXPORT:
      return "RCT_TYPE_PRODUCER_EX";
    case RCT_TYPE_WORKER_ADAPTER:
      return "RCT_TYPE_WORKER_AD  ";
    default:
      iwlog_warn("Unknown resource type: 0x%x", type);
      return "????????????????";
  }
}

void rct_resource_get_worker_id_lk(void *v, wrc_resource_t *out_worker_id) {
  assert(v);
  rct_resource_base_t *b = v;
  int resource_type = b->type;
  switch (resource_type) {
    case RCT_TYPE_ROUTER:
      *out_worker_id = ((rct_router_t*) b)->worker_id;
      break;
    case RCT_TYPE_PRODUCER:
    case RCT_TYPE_PRODUCER_DATA:
      *out_worker_id = ((rct_producer_base_t*) b)->transport->router->worker_id;
      break;
    case RCT_TYPE_CONSUMER:
    case RCT_TYPE_CONSUMER_DATA:
      *out_worker_id = ((rct_consumer_base_t*) b)->producer->transport->router->worker_id;
      break;
    case RCT_TYPE_TRANSPORT_WEBRTC:
    case RCT_TYPE_TRANSPORT_DIRECT:
    case RCT_TYPE_TRANSPORT_PLAIN:
    case RCT_TYPE_TRANSPORT_PIPE:
      *out_worker_id = ((rct_transport_t*) b)->router->worker_id;
      break;
    case RCT_TYPE_OBSERVER_AL:
    case RCT_TYPE_OBSERVER_AS:
      *out_worker_id = ((rct_rtp_observer_t*) b)->router->worker_id;
      break;
    case RCT_TYPE_WORKER_ADAPTER:
      *out_worker_id = b->id;
      break;
    default:
      iwlog_error("Failed find worker id for resource of type: 0x%x", resource_type);
      *out_worker_id = 0;
      break;
  }
}

void* rct_resource_ref_lk(void *v, int nrefs, const char *tag) {
  if (v) {
    rct_resource_base_t *b = v;
    if (!b->id) {
      b->id = (uintptr_t) b;
    }

#ifndef NDEBUG
    iwlog_debug("\tRCT REF [%s]: 0x%" PRIx64 ":\t%" PRId64 " => %" PRId64 "\t%s",
                rct_resource_type_name(b->type), b->id, b->refs, b->refs + nrefs,
                tag ? tag : "");
#endif // !NDEBUG

    b->refs += nrefs;
    if (b->refs < 0) {
      iwlog_ecode_debug3(RCT_ERROR_UNBALANCED_REFS);
    }
    if (b->refs < 1) {
      _resource_dispose_lk(b);
      v = 0;
    }
  }
  return v;
}

void rct_resource_ref_unlock(void *b, bool locked, int nrefs, const char *tag) {
  if (!locked) {
    _lock();
  }
  rct_resource_ref_lk(b, nrefs, tag);
  _unlock();
}

void rct_resource_ref_keep_locking(void *b, bool locked, int nrefs, const char *tag) {
  if (locked) {
    rct_resource_ref_lk(b, nrefs, tag);
  } else {
    rct_resource_ref_unlock(b, false, nrefs, tag);
  }
}

void* rct_resource_ref_locked(void *b, int nrefs, const char *tag) {
  _lock();
  rct_resource_ref_lk(b, nrefs, tag);
  return b;
}

void rct_resource_unlock(void *b, const char *tag) {
  if (b) {
    rct_resource_ref_lk(b, -1, tag);
  }
  _unlock();
}

void* rct_resource_by_id_locked_exists(wrc_resource_t resource_id, int type, const char *tag) {
  _lock();
  rct_resource_base_t *b = rct_resource_ref_lk(rct_resource_by_id_unsafe(resource_id, type), 1, tag);
  if (!b) {
    _unlock();
  }
  return b;
}

void rct_resource_unlock_exists(void *b, const char *tag) {
  if (b) {
    rct_resource_ref_lk(b, -1, tag);
    _unlock();
  }
}

void rct_resource_unlock_keep_ref(void *b) {
  _unlock();
}

iwrc rct_resource_register_lk(void *v) {
  iwrc rc = 0;
  rct_resource_base_t *b = v;
  if (!b) {
    return IW_ERROR_INVALID_ARGS;
  }
  assert(b->type > 0 && b->type < RCT_TYPE_UPPER);
  if (!b->id) {
    b->id = (uintptr_t) b;
  }
  if (!b->uuid[0]) {
    iwu_uuid4_fill(b->uuid);
  }

  rct_resource_base_t *old = iwhmap_get_u64(state.map_id2ptr, b->id);
  if (old) {
    iwlog_error("RCT Double registration of resource: %s type: 0x%x", old->uuid, old->type);
  }

  RCC(rc, finish, iwhmap_put(state.map_uuid2ptr, b->uuid, b));
  RCC(rc, finish, iwhmap_put_u64(state.map_id2ptr, b->id, b));

  wrc_resource_t wid = 0;
  if (b->type == RCT_TYPE_ROUTER) {
    wid = ((rct_router_t*) b)->worker_id;
  } else if (b->type & RCT_TYPE_TRANSPORT_ALL) {
    wid = ((rct_transport_t*) b)->router->worker_id;
  } else if (b->type & RCT_TYPE_CONSUMER_ALL) {
    wid = ((rct_consumer_base_t*) b)->producer->transport->router->worker_id;
  } else if (b->type & RCT_TYPE_PRODUCER_ALL) {
    wid = ((rct_producer_base_t*) b)->transport->router->worker_id;
  }
  if (wid) {
    b->wid = wid;
    wrc_ajust_load_score(wid, 1);
  }

  _resource_gauge_update_lk(b);

finish:
  return rc;
}

int rct_resource_get_number_of_type_lk(int type) {
  int num = 0;
  IWHMAP_ITER iter;
  iwhmap_iter_init(state.map_id2ptr, &iter);
  while (iwhmap_iter_next(&iter)) {
    const rct_resource_base_t *b = iter.val;
    if (b && (b->type & type)) {
      ++num;
    }
  }
  return num;
}

iwrc rct_set_resource_data(wrc_resource_t resource_id, const char *data) {
  iwrc rc = 0;
  rct_resource_base_t *b = rct_resource_by_id_locked(resource_id, 0, __func__);
  if (b) {
    if (b->user_data) {
      free(b->user_data);
      b->user_data = 0;
    }
    if (data) {
      RCB(finish, b->user_data = strdup(data));
    }
  }

finish:
  rct_resource_unlock(b, __func__);
  return rc;
}

char* rct_get_resource_data_copy(wrc_resource_t resource_id) {
  char *result = 0;
  rct_resource_base_t *b = rct_resource_by_id_locked(resource_id, 0, __func__);
  if (b && b->user_data) {
    result = strdup(b->user_data);
  }
  rct_resource_unlock(b, __func__);
  return result;
}

iwrc rct_resource_probe_by_uuid(const char *resource_uuid, rct_resource_base_t *b) {
  if (!resource_uuid || !b) {
    return IW_ERROR_INVALID_ARGS;
  }
  iwrc rc = 0;
  _lock();
  rct_resource_base_t *rp = iwhmap_get(state.map_uuid2ptr, resource_uuid);
  if (!rp) {
    memset(b, 0, sizeof(*b));
    rc = IW_ERROR_NOT_EXISTS;
  } else {
    memcpy(b, rp, sizeof(*b));
  }
  _unlock();
  return rc;
}

iwrc rct_resource_probe_by_id(wrc_resource_t resource_id, rct_resource_base_t *b) {
  if (!b) {
    return IW_ERROR_INVALID_ARGS;
  }
  iwrc rc = 0;
  _lock();
  rct_resource_base_t *rp = iwhmap_get_u64(state.map_id2ptr, resource_id);
  if (!rp) {
    memset(b, 0, sizeof(*b));
    rc = IW_ERROR_NOT_EXISTS;
  } else {
    memcpy(b, rp, sizeof(*b));
  }
  _unlock();
  return rc;
}

void* rct_resource_by_uuid_unsafe(const char *resource_uuid, int type) {
  if (!resource_uuid) {
    return 0;
  }
  rct_resource_base_t *rp = iwhmap_get(state.map_uuid2ptr, resource_uuid);
  if (rp && type && (rp->type != type)) {
    iwlog_error("RTC Type: %d of resource: 0x%" PRIx64 ", %s doesn't much required type: 0x%x",
                rp->type,
                rp->id,
                rp->uuid,
                type);
    return 0;
  }
  return rp;
}

void* rct_resource_by_id_unsafe(wrc_resource_t resource_id, int type) {
  rct_resource_base_t *rp = iwhmap_get_u64(state.map_id2ptr, resource_id);
  if (rp && type && !(rp->type & type)) {
    return 0;
  }
  return rp;
}

void* rct_resource_by_uuid_locked(const char *resource_uuid, int type, const char *tag) {
  _lock();
  return rct_resource_ref_lk(rct_resource_by_uuid_unsafe(resource_uuid, type), 1, tag);
}

void* rct_resource_by_id_locked(wrc_resource_t resource_id, int type, const char *tag) {
  _lock();
  return rct_resource_ref_lk(rct_resource_by_id_unsafe(resource_id, type), 1, tag);
}

void* rct_resource_by_id_unlocked(wrc_resource_t resource_id, int type, const char *tag) {
  _lock();
  void *ret = rct_resource_ref_lk(rct_resource_by_id_unsafe(resource_id, type), 1, tag);
  _unlock();
  return ret;
}

void* rct_resource_by_id_locked_lk(wrc_resource_t resource_id, int type, const char *tag) {
  return rct_resource_ref_lk(rct_resource_by_id_unsafe(resource_id, type), 1, tag);
}

iwrc rct_resource_get_identity(
  wrc_resource_t resource_id, int resource_type, JBL *identity_out,
  wrc_resource_t *worker_id_out
  ) {
  iwrc rc = 0;
  *identity_out = 0;
  *worker_id_out = 0;
  rct_resource_base_t *b = rct_resource_by_id_locked(resource_id, resource_type, __func__);
  RCIF(!b, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);
  RCC(rc, finish, jbl_clone(b->identity, identity_out));
  rct_resource_get_worker_id_lk(b, worker_id_out);

finish:
  rct_resource_unlock(b, __func__);
  return rc;
}

iwrc rct_resource_json_command2(struct rct_json_command_spec *spec) {
  JBL cmd_data = spec->cmd_data, *cmd_out = spec->cmd_out;
  int resource_type = spec->resource_type;
  wrc_resource_t resource_id = spec->resource_id;
  wrc_worker_cmd_e cmd = spec->cmd;
  wrc_msg_t *m = 0;

  if (cmd_out) {
    *cmd_out = 0;
  }

  JBL identity;
  wrc_resource_t worker_id;
  iwrc rc = RCR(rct_resource_get_identity(resource_id, resource_type, &identity, &worker_id));

  if (spec->identity_extra) {
    RCC(rc, finish, jbl_object_copy_to(spec->identity_extra, identity));
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = worker_id,
    .input = {
      .worker     = {
        .internal = identity,
        .cmd      = cmd,
        .data     = cmd_data
      }
    }
  }));

  if (!spec->async) {
    rc = wrc_send_and_wait(m, 0);
    if (!rc && cmd_out) {
      *cmd_out = m->output.worker.data;
      m->output.worker.data = 0; // Move ownership to cmd_out
    }
  } else {
    if (cmd_data) {
      RCC(rc, finish, jbl_clone(cmd_data, &m->input.worker.data));
    }
    rc = wrc_send(m);
  }

finish:
  if (!spec->async && m) {
    m->input.worker.data = 0;
    wrc_msg_destroy(m);
  }
  return rc;
}

iwrc rct_resource_json_command(
  wrc_resource_t   resource_id,
  wrc_worker_cmd_e cmd,
  int              resource_type,
  JBL              cmd_data,
  JBL             *cmd_out
  ) {
  return rct_resource_json_command2(&(struct rct_json_command_spec) {
    .resource_id = resource_id,
    .cmd = cmd,
    .resource_type = resource_type,
    .cmd_data = cmd_data,
    .cmd_out = cmd_out
  });
}

iwrc rct_resource_json_command_async(
  wrc_resource_t   resource_id,
  wrc_worker_cmd_e cmd,
  int              resource_type,
  JBL              cmd_data
  ) {
  return rct_resource_json_command2(&(struct rct_json_command_spec) {
    .resource_id = resource_id,
    .cmd = cmd,
    .resource_type = resource_type,
    .cmd_data = cmd_data,
    .async = true
  });
}

#define REPORT(msg_)                         \
  iwlog_error2(msg_);                        \
  return RCT_ERROR_INVALID_RTP_PARAMETERS;

bool rct_codec_is_rtx(JBL_NODE n) {
  JBL_NODE m;
  iwrc rc = jbn_at(n, "/mimeType", &m);
  if (rc || (m->type != JBV_STR) || (m->vsize < 5)) {
    return false;
  }
  return !strncmp("/rtx", m->vptr + m->vsize - 4, 4);
}

iwrc rct_validate_rtcp_feedback(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("fb is not an object");
  }
  iwrc rc = jbn_at(n, "/type", &r);
  if (rc || (r->type != JBV_STR)) {
    REPORT("missing fb.type");
  }
  rc = jbn_at(n, "/parameter", &r);
  if (rc) {
    rc = RCR(jbn_add_item_str(n, "parameter", "", 0, &r, pool));
  } else if (r->type != JBV_STR) {
    r->type = JBV_STR;
    r->child = 0;
    r->vsize = 0;
  }
  return 0;
}

#undef REPORT

static iwrc _event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  iwrc rc = 0;
  switch (evt) {
    case WRC_EVT_PAYLOAD:
      break;
    case WRC_EVT_STARTED:
      if (g_env.log.verbose) {
        iwlog_info2("RCT WRC_EVT_STARTED");
      }
      break;
    case WRC_EVT_SHUTDOWN:
      if (g_env.log.verbose) {
        iwlog_info2("RCT WRC_EVT_SHUTDOWN");
      }
      break;
    case WRC_EVT_WORKER_SHUTDOWN:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_WORKER_SHUTDOWN: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_WORKER_LAUNCHED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_WORKER_LAUNCHED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROUTER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROUTER_CREATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROUTER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROUTER_CLOSED: 0x%" PRIx64, resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_TRANSPORT_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_CREATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_UPDATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_UPDATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_CLOSED: 0x%" PRIx64, resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_TRANSPORT_ICE_STATE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_ICE_STATE_CHANGE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_ICE_SELECTED_TUPLE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_ICE_SELECTED_TUPLE_CHANGE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_DTLS_STATE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_DTLS_STATE_CHANGE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_SCTP_STATE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_SCTP_STATE_CHANGE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_TUPLE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_TUPLE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_RTCPTUPLE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_RTCPTUPLE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_CREATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_VIDEO_ORIENTATION_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_VIDEO_ORIENTATION_CHANGE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_CLOSED: 0x%" PRIx64, resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_PRODUCER_PAUSE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_PAUSE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_RESUME:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_RESUME: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_PRODUCER_PAUSE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_PRODUCER_PAUSE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_PRODUCER_RESUME:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_PRODUCER_RESUME: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_RESOURCE_SCORE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_RESOURCE_SCORE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_LAYERSCHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_LAYERSCHANGE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_PAUSE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_PAUSE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_CLOSED: 0x%" PRIx64, resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_CONSUMER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_CREATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_RESUME:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_RESUME: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_AUDIO_OBSERVER_SILENCE:
      // if (g_env.log.verbose) {
      //   iwlog_info("RCT WRC_EVT_AUDIO_OBSERVER_SILENCE: 0x%" PRIx64, resource_id);
      // }
      break;
    case WRC_EVT_AUDIO_OBSERVER_VOLUMES:
      // if (g_env.log.verbose) {
      //   iwlog_info("RCT WRC_EVT_AUDIO_OBSERVER_VOLUMES: 0x%" PRIx64, resource_id);
      // }
      break;
    case WRC_EVT_ACTIVE_SPEAKER:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ACTIVE_SPEAKER: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_CREATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_CLOSED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_JOIN:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_JOIN: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_LEFT:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_LEFT: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_MUTE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_MUTE: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_MSG:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_MSG: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_CREATED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_PAUSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_PAUSED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_RESUMED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_RESUMED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_CLOSED: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_RECORDING_ON:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_RECORDING_ON: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_RECORDING_OFF:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_RECORDING_OFF: 0x%" PRIx64, resource_id);
      }
      break;
    case WRC_EVT_ROOM_RECORDING_PP:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_RECORDING_PP");
      }
      break;
    default:
      iwlog_warn("RCT Unknown event: %d resource_id: 0x%" PRIx64, evt, resource_id);
      break;
  }
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc;
}

///////////////////////////////////////////////////////////////////////////
//                         Initialization                                //
///////////////////////////////////////////////////////////////////////////

static wrc_resource_t _rct_wrc_uuid_resolver(const char *uuid) {
  rct_resource_base_t b;
  iwrc rc = rct_resource_probe_by_uuid(uuid, &b);
  if (rc == IW_ERROR_NOT_EXISTS) {
    return 0;
  } else if (rc) {
    iwlog_ecode_error3(rc);
    return 0;
  }
  return b.id;
}

static const char* _ecodefn(locale_t locale, uint32_t ecode) {
  if (!((ecode > _RCT_ERROR_START) && (ecode < _RCT_ERROR_END))) {
    return 0;
  }
  switch (ecode) {
    case RCT_ERROR_TOO_MANY_DYNAMIC_PAYLOADS:
      return "Too many dynamic payload types (RCT_ERROR_TOO_MANY_DYNAMIC_PAYLOADS)";
    case RCT_ERROR_INVALID_RTP_PARAMETERS:
      return "Invalid RTP parameters (RCT_ERROR_INVALID_RTP_PARAMETERS)";
    case RCT_ERROR_INVALID_PROFILE_LEVEL_ID:
      return "Invalid H264 profile level ID (RCT_ERROR_INVALID_PROFILE_LEVEL_ID)";
    case RCT_ERROR_PROFILE_LEVEL_ID_MISMATCH:
      return "H264 profile level ID mismatch (RCT_ERROR_PROFILE_LEVEL_ID_MISMATCH)";
    case RCT_ERROR_UNSUPPORTED_CODEC:
      return "Unsupported codec (RCT_ERROR_UNSUPPORTED_CODEC)";
    case RCT_ERROR_NO_RTX_ASSOCIATED_CODEC:
      return "Cannot find assiciated media codec for given RTX payload type (RCT_ERROR_NO_RTX_ASSOCIATED_CODEC)";
    case RCT_ERROR_INVALID_SCTP_STREAM_PARAMETERS:
      return "Invalid SCTP Stream parameters (RCT_ERROR_INVALID_SCTP_STREAM_PARAMETERS)";
    case RCT_ERROR_UNBALANCED_REFS:
      return "Unbalanced resource refs (RCT_ERROR_UNBALANCED_REFS)";
    case RCT_ERROR_INVALID_RESOURCE_CONFIGURATION:
      return "Invalid resource configuration (RCT_ERROR_INVALID_RESOURCE_CONFIGURATION)";
    case RCT_ERROR_REQUIRED_DIRECT_TRANSPORT:
      return
        "Transport must be of RCT_TYPE_TRANSPORT_DIRECT type to perform operation (RCT_ERROR_REQUIRED_DIRECT_TRANSPORT)";
  }
  return 0;
}

static void _destroy_lk(void) {
  wrc_register_uuid_resolver(0);
  if (state.map_id2ptr) {
    iwhmap_destroy(state.map_id2ptr);
    state.map_id2ptr = 0;
  }
  if (state.map_uuid2ptr) {
    iwhmap_destroy(state.map_uuid2ptr);
    state.map_uuid2ptr = 0;
  }
  iwpool_destroy(state.pool);
  state.pool = 0;
}

iwrc rct_init() {
  iwrc rc = RCR(iwlog_register_ecodefn(_ecodefn));

  RCB(finish, state.pool = iwpool_create(512));
  RCB(finish, state.map_id2ptr = iwhmap_create_u64(0));
  RCB(finish, state.map_uuid2ptr = iwhmap_create_str(0));

  RCC(rc, finish, jbn_from_json((void*) data_supported_rtp_capabilities,
                                &state.available_capabilities, state.pool));

  wrc_register_uuid_resolver(_rct_wrc_uuid_resolver);

  RCC(rc, finish, rct_worker_module_init());
  RCC(rc, finish, rct_transport_module_init());
  RCC(rc, finish, rct_consumer_module_init());
  RCC(rc, finish, rct_producer_export_module_init());
  RCC(rc, finish, rct_room_module_init());

  // Add as last event handler
  RCC(rc, finish, wrc_add_event_handler(_event_handler, 0, &state.event_handler_id));

finish:
  if (rc) {
    _destroy_lk();
  }
  return rc;
}

void rct_shutdown(void) {
  rct_worker_module_shutdown();
}

void rct_destroy(void) {
  wrc_remove_event_handler(state.event_handler_id);
  rct_producer_export_module_destroy();
  rct_consumer_module_destroy();
  rct_room_module_destroy();
  rct_transport_module_destroy();
  rct_worker_module_destroy();
  _destroy_lk();
}
