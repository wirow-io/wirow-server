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

#include <iowow/iwre.h>
#include <iowow/iwconv.h>
#include <iowow/iwre.h>

#include <assert.h>

#include "data_supported_rtp_capabilities.inc"

struct rct_state rct_state = {
  .mtx = PTHREAD_MUTEX_INITIALIZER
};

IW_INLINE void _lock(void) {
  pthread_mutex_lock(&rct_state.mtx);
}

IW_INLINE void _unlock(void) {
  pthread_mutex_unlock(&rct_state.mtx);
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
  rct_resource_base_t *b = iwhmap_get(rct_state.map_id2ptr, (void*) (intptr_t) resource_id);
  if (b) {
    iwhmap_remove(rct_state.map_id2ptr, (void*) (intptr_t) resource_id);
    iwhmap_remove(rct_state.map_uuid2ptr, b->uuid);
    if (b->wid) {
      wrc_ajust_load_score(b->wid, -1);
    }
    _resource_gauge_update_lk(b);
  }
}

static void _resource_dispose_lk(rct_resource_base_t *b) {
  assert(b->refs < 1);
  iwlog_debug("RCT Dispose [%s]: %" PRIu32, rct_resource_type_name(b->type), b->id);

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

  iwlog_debug("RCT Close  [%s]: %" PRIu32, rct_resource_type_name(b->type), b->id);
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
    case RCT_TYPE_PRODUCER_EXPORT:
      return "RCT_TYPE_PRODUCER_EX";
    default:
      iwlog_warn("Unknown resource type: %d", type);
      return "????????????????";
  }
}

void rct_resource_get_worker_id_lk(void *v, wrc_resource_t *worker_id_out) {
  assert(v);
  rct_resource_base_t *b = v;
  int resource_type = b->type;
  switch (resource_type) {
    case RCT_TYPE_ROUTER:
      *worker_id_out = ((rct_router_t*) b)->worker_id;
      break;
    case RCT_TYPE_PRODUCER:
    case RCT_TYPE_PRODUCER_DATA:
      *worker_id_out = ((rct_producer_base_t*) b)->transport->router->worker_id;
      break;
    case RCT_TYPE_CONSUMER:
    case RCT_TYPE_CONSUMER_DATA:
      *worker_id_out = ((rct_consumer_base_t*) b)->producer->transport->router->worker_id;
      break;
    case RCT_TYPE_TRANSPORT_WEBRTC:
    case RCT_TYPE_TRANSPORT_DIRECT:
    case RCT_TYPE_TRANSPORT_PLAIN:
    case RCT_TYPE_TRANSPORT_PIPE:
      *worker_id_out = ((rct_transport_t*) b)->router->worker_id;
      break;
    case RCT_TYPE_OBSERVER_AL:
    case RCT_TYPE_OBSERVER_AS:
      *worker_id_out = ((rct_rtp_observer_t*) b)->router->worker_id;
      break;
    default:
      iwlog_error("Failed find worker id for resource of type: %d", resource_type);
      *worker_id_out = 0;
      break;
  }
}

void* rct_resource_ref_lk(void *v, int nrefs, const char *tag) {
  if (v) {
    rct_resource_base_t *b = v;

#ifndef NDEBUG
    iwlog_debug("\tRCT REF [%s]: %" PRIu32 ":\t%" PRId64 " => %" PRId64 "\t%s",
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
  rct_resource_base_t *b = v;
  if (!b) {
    return IW_ERROR_INVALID_ARGS;
  }
  assert(b->type > 0 && b->type < RCT_TYPE_UPPER);

  rct_resource_base_t *old = iwhmap_get(rct_state.map_uuid2ptr, b->uuid);
  if (old) {
    iwlog_error("RCT Double registration of resource: %s type: 0x%x", old->uuid, old->type);
  }
  iwrc rc = iwhmap_put(rct_state.map_uuid2ptr, b->uuid, b);
  RCGO(rc, finish);
  RCC(rc, finish, iwhmap_put(rct_state.map_id2ptr, (void*) (intptr_t) b->id, b));

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
  void *val;
  IWHMAP_ITER iter;
  iwhmap_iter_init(rct_state.map_id2ptr, &iter);
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
  rct_resource_base_t *rp = iwhmap_get(rct_state.map_uuid2ptr, resource_uuid);
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
  rct_resource_base_t *rp = iwhmap_get(rct_state.map_id2ptr, (void*) (intptr_t) resource_id);
  if (!rp) {
    memset(b, 0, sizeof(*b));
    rc = IW_ERROR_NOT_EXISTS;
  } else {
    memcpy(b, rp, sizeof(*b));
  }
  _unlock();
  return rc;
}

wrc_resource_t rct_resource_id_next_lk(void) {
  if (rct_state.resource_seq == ((uint32_t) -1) >> 1) {
    rct_state.resource_seq = 0;
  }
  return ++rct_state.resource_seq;
}

void* rct_resource_by_uuid_unsafe(const char *resource_uuid, int type) {
  if (!resource_uuid) {
    return 0;
  }
  rct_resource_base_t *rp = iwhmap_get(rct_state.map_uuid2ptr, resource_uuid);
  if (rp && type && (rp->type != type)) {
    iwlog_error("RTC Type: %d of resource: %d,%s doesn't much required type: %d", rp->type, rp->id, rp->uuid, type);
    return 0;
  }
  return rp;
}

void* rct_resource_by_id_unsafe(wrc_resource_t resource_id, int type) {
  rct_resource_base_t *rp = iwhmap_get(rct_state.map_id2ptr, (void*) (intptr_t) resource_id);
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
    .resource_id = worker_id,
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

iwrc rct_codecs_is_matched(JBL_NODE ac, JBL_NODE bc, bool strict, bool modify, IWPOOL *pool, bool *res) {
  *res = false;
  iwrc rc = 0;
  JBL_NODE n1, n2;
  int64_t iv1, iv2;
  const char *mime_type;

  // Compare mime types
  rc = jbn_at(ac, "/mimeType", &n1);
  if (rc || (n1->type != JBV_STR)) {
    return 0;
  }
  rc = jbn_at(bc, "/mimeType", &n2);
  if (rc || (n2->type != n1->type) || (n2->vsize != n1->vsize) || utf8ncasecmp(n1->vptr, n2->vptr, n1->vsize)) {
    return 0;
  }
  mime_type = n1->vptr;

  if (jbn_path_compare(ac, bc, "/clockRate", 0, &rc)) {
    return 0;
  }
  if (jbn_path_compare(ac, bc, "/channels", 0, &rc)) {
    return 0;
  }

  // Checks specific to some codecs
  if (!strcasecmp(mime_type, "video/h264")) {
    iv1 = 0, iv2 = 0;
    rc = jbn_at(ac, "/parameters/packetization-mode", &n1);
    if (!rc && (n1->type == JBV_I64)) {
      iv1 = n1->vi64;
    }
    rc = jbn_at(bc, "/parameters/packetization-mode", &n2);
    if (!rc && (n2->type == JBV_I64)) {
      iv2 = n2->vi64;
    }
    if (iv1 != iv2) {
      return 0;
    }
    ;
    if (strict) {
      h264_plid_t selected_plid;
      rc = jbn_at(ac, "/parameters/profile-level-id", &n1);
      if (rc || (n1->type != JBV_STR)) {
        n1 = 0;
      }
      rc = jbn_at(bc, "/parameters/profile-level-id", &n2);
      if (rc || (n2->type != JBV_STR)) {
        n2 = 0;
      }
      if (!h264_plid_equal(n1 ? n1->vptr : 0, n1 ? n1->vsize : 0, n2 ? n2->vptr : 0, n2 ? n2->vsize : 0)) {
        return 0;
      }
      rc = jbn_at(ac, "/parameters", &n1);
      if (rc) {
        return 0;
      }
      jbn_at(bc, "/parameters", &n2);
      if (rc) {
        return 0;
      }
      rc = h256_plid_to_answer(n1, n2, &selected_plid);
      if (rc) {
        return 0;
      }
      if (modify) {
        if (selected_plid.profile) {
          char plevel[6];
          RCR(h256_plid_write(&selected_plid, plevel));
          RCR(jbn_copy_path(&(struct _JBL_NODE) {
            .type = JBV_STR,
            .vsize = sizeof(plevel),
            .vptr = plevel
          }, "/", ac, "/parameters/profile-level-id", false, false, pool));
        } else {
          jbn_detach(ac, "/parameters/profile-level-id");
        }
      }
    }
  } else if (!strcasecmp(mime_type, "video/vp9")) {
    if (strict) {
      iv1 = 0, iv2 = 0;
      rc = jbn_at(ac, "/parameters/profile-id", &n1);
      if (!rc && (n1->type == JBV_I64)) {
        iv1 = n1->vi64;
      }
      rc = jbn_at(bc, "/parameters/profile-id", &n2);
      if (!rc && (n2->type == JBV_I64)) {
        iv2 = n2->vi64;
      }
      if (iv1 != iv2) {
        return 0;
      }
    }
  }
  *res = true;
  return 0;
}

#undef REPORT

void rct_parse_scalability_mode(
  const char *mode,
  int        *spartial_layers_out,
  int        *temporal_layers_out,
  bool       *ksvc_out
  ) {
  if (spartial_layers_out) {
    *spartial_layers_out = 1;
  }
  if (temporal_layers_out) {
    *temporal_layers_out = 1;
  }
  if (ksvc_out) {
    *ksvc_out = false;
  }
  if (!mode) {
    return;
  }

  char buf[3];
  const char *mpairs[IWRE_MAX_MATCHES];
  struct iwre *re = iwre_create("[LS]([1-9][0-9]?)T([1-9][0-9]?)(_KEY)?");
  if (!re) {
    return;
  }
  int nm = iwre_match(re, mode, mpairs, IWRE_MAX_MATCHES);
  if (nm < 3) {
    goto finish;
  }

  if (spartial_layers_out) {
    if (mpairs[3] - mpairs[2] < sizeof(buf)) {
      memcpy(buf, mpairs[2], mpairs[3] - mpairs[2]);
      buf[mpairs[3] - mpairs[2]] = '\0';
      *spartial_layers_out = (int) iwatoi(buf);
    } else {
      goto finish;
    }
  }

  if (temporal_layers_out) {
    if (mpairs[5] - mpairs[4] < sizeof(buf)) {
      memcpy(buf, mpairs[4], mpairs[5] - mpairs[4]);
      buf[mpairs[5] - mpairs[4]] = '\0';
      *temporal_layers_out = (int) iwatoi(buf);
    } else {
      goto finish;
    }
  }
  if (ksvc_out && nm >= 4) {
    *ksvc_out = mpairs[7] - mpairs[6] > 0;
  }

finish:
  iwre_destroy(re);
}

const char* rct_hash_func_name(rct_hash_func_e hf) {
  switch (hf) {
    case RCT_SHA256:
      return "sha-256";
    case RCT_SHA512:
      return "sha-512";
    case RCT_SHA384:
      return "sha-384";
    case RCT_SHA224:
      return "sha-224";
    case RCT_SHA1:
      return "sha-1";
    case RCT_MD2:
      return "md2";
    case RCT_MD5:
      return "md5";
    default:
      return "unknown";
  }
}

rct_hash_func_e rct_name_to_hash_func(const char *name) {
  if (!strcmp(name, "sha-256")) {
    return RCT_SHA256;
  } else if (!strcmp(name, "sha-512")) {
    return RCT_SHA512;
  } else if (!strcmp(name, "sha-384")) {
    return RCT_SHA384;
  } else if (!strcmp(name, "sha-224")) {
    return RCT_SHA224;
  } else if (!strcmp(name, "sha-1")) {
    return RCT_SHA1;
  } else if (!strcmp(name, "md2")) {
    return RCT_MD2;
  } else if (!strcmp(name, "md5")) {
    return RCT_MD5;
  } else {
    return 0;
  }
}

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
        iwlog_info("RCT WRC_EVT_WORKER_SHUTDOWN: %u", resource_id);
      }
      break;
    case WRC_EVT_WORKER_LAUNCHED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_WORKER_LAUNCHED: %u", resource_id);
      }
      break;
    case WRC_EVT_ROUTER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROUTER_CREATED: %u", resource_id);
      }
      break;
    case WRC_EVT_ROUTER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROUTER_CLOSED: %u", resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_TRANSPORT_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_CREATED: %u", resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_UPDATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_UPDATED: %u", resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_CLOSED: %u", resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_TRANSPORT_ICE_STATE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_ICE_STATE_CHANGE: %u", resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_ICE_SELECTED_TUPLE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_ICE_SELECTED_TUPLE_CHANGE: %u", resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_DTLS_STATE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_DTLS_STATE_CHANGE: %u", resource_id);
      }

      break;
    case WRC_EVT_TRANSPORT_SCTP_STATE_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_SCTP_STATE_CHANGE: %u", resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_TUPLE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_TUPLE: %u", resource_id);
      }
      break;
    case WRC_EVT_TRANSPORT_RTCPTUPLE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_TRANSPORT_RTCPTUPLE: %u", resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_CREATED: %u", resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_VIDEO_ORIENTATION_CHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_VIDEO_ORIENTATION_CHANGE: %u", resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_CLOSED: %u", resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_PRODUCER_PAUSE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_PAUSE: %u", resource_id);
      }
      break;
    case WRC_EVT_PRODUCER_RESUME:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_PRODUCER_RESUME: %u", resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_PRODUCER_PAUSE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_PRODUCER_PAUSE: %u", resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_PRODUCER_RESUME:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_PRODUCER_RESUME: %u", resource_id);
      }
      break;
    case WRC_EVT_RESOURCE_SCORE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_RESOURCE_SCORE: %u", resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_LAYERSCHANGE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_LAYERSCHANGE: %u", resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_PAUSE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_PAUSE: %u", resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_CLOSED: %u", resource_id);
      }
      _resource_close(resource_id);
      break;
    case WRC_EVT_CONSUMER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_CREATED: %u", resource_id);
      }
      break;
    case WRC_EVT_CONSUMER_RESUME:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_CONSUMER_RESUME: %u", resource_id);
      }
      break;
    case WRC_EVT_AUDIO_OBSERVER_SILENCE:
      // if (g_env.log.verbose) {
      //   iwlog_info("RCT WRC_EVT_AUDIO_OBSERVER_SILENCE: %u", resource_id);
      // }
      break;
    case WRC_EVT_AUDIO_OBSERVER_VOLUMES:
      // if (g_env.log.verbose) {
      //   iwlog_info("RCT WRC_EVT_AUDIO_OBSERVER_VOLUMES: %u", resource_id);
      // }
      break;
    case WRC_EVT_ACTIVE_SPEAKER:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ACTIVE_SPEAKER: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_CREATED: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_CLOSED: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_JOIN:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_JOIN: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_LEFT:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_LEFT: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_MUTE:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_MUTE: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_MEMBER_MSG:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_MEMBER_MSG: %u", resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_CREATED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_CREATED: %u", resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_PAUSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_PAUSED: %u", resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_RESUMED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_RESUMED: %u", resource_id);
      }
      break;
    case WRC_EVT_OBSERVER_CLOSED:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_OBSERVER_CLOSED: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_RECORDING_ON:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_RECORDING_ON: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_RECORDING_OFF:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_RECORDING_OFF: %u", resource_id);
      }
      break;
    case WRC_EVT_ROOM_RECORDING_PP:
      if (g_env.log.verbose) {
        iwlog_info("RCT WRC_EVT_ROOM_RECORDING_PP");
      }
      break;
    default:
      iwlog_warn("RCT Unknown event: %d resource_id: %u", evt, resource_id);
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
  if (rct_state.map_id2ptr) {
    iwhmap_destroy(rct_state.map_id2ptr);
    rct_state.map_id2ptr = 0;
  }
  if (rct_state.map_uuid2ptr) {
    iwhmap_destroy(rct_state.map_uuid2ptr);
    rct_state.map_uuid2ptr = 0;
  }
  iwpool_destroy(rct_state.pool);
  rct_state.pool = 0;
}

iwrc rct_init() {
  iwrc rc = RCR(iwlog_register_ecodefn(_ecodefn));

  RCB(finish, rct_state.pool = iwpool_create(512));
  RCB(finish, rct_state.map_id2ptr = iwhmap_create_i32(0));
  RCB(finish, rct_state.map_uuid2ptr = iwhmap_create_str(0));

  RCC(rc, finish, jbn_from_json((void*) data_supported_rtp_capabilities,
                                &rct_state.available_capabilities, rct_state.pool));

  wrc_register_uuid_resolver(_rct_wrc_uuid_resolver);

  RCC(rc, finish, rct_worker_module_init());
  RCC(rc, finish, rct_transport_module_init());
  RCC(rc, finish, rct_consumer_module_init());
  RCC(rc, finish, rct_producer_export_module_init());
  RCC(rc, finish, rct_room_module_init());


  // Add as last event handler
  RCC(rc, finish, wrc_add_event_handler(_event_handler, 0, &rct_state.event_handler_id));


finish:
  if (rc) {
    _destroy_lk();
  }
  return rc;
}

void rct_shutdown(void) {
  rct_worker_module_shutdown();
}

void rct_close(void) {
  wrc_remove_event_handler(rct_state.event_handler_id);
  rct_producer_export_module_close();
  rct_consumer_module_close();
  rct_room_module_close();
  rct_transport_module_close();
  rct_worker_module_close();
  _destroy_lk();
}
