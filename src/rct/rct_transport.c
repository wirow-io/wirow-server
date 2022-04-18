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

#include "rct_transport.h"
#include <assert.h>

iwrc _rct_transport_webrtc_connect(rct_transport_webrtc_t *transport, rtc_transport_webrtc_connect_t *spec);

void rct_transport_dispose_lk(rct_transport_t *t) {
  assert(t);
  if (t->router) {
    for (rct_transport_t *p = t->router->transports, *pp = 0; p; p = p->next) {
      if (p->id == t->id) {
        if (pp) {
          pp->next = p->next;
        } else {
          t->router->transports = p->next;
        }
      } else {
        pp = p;
      }
    }
  }
  rct_resource_ref_lk(t->router, -1, __func__); // Unref parent router
}

void rct_transport_close_lk(rct_transport_t *t) {
  assert(t);
  for (rct_producer_base_t *p = t->producers, *n; p; p = n) {
    n = p->next;
    rct_resource_close_lk(p);
  }
  for (rct_consumer_base_t *p = t->consumers, *n; p; p = n) {
    n = p->next_transport;
    rct_resource_close_lk(p);
  }
}

iwrc rct_transport_close(wrc_resource_t transport_id) {
  iwrc rc = rct_resource_json_command(transport_id, WRC_CMD_TRANSPORT_CLOSE, 0, 0, 0);
  wrc_notify_event_handlers(WRC_EVT_TRANSPORT_CLOSED, transport_id, 0);
  return rc;
}

iwrc rct_transport_close_async(wrc_resource_t transport_id) {
  iwrc rc = rct_resource_json_command_async(transport_id, WRC_CMD_TRANSPORT_CLOSE, 0, 0);
  wrc_notify_event_handlers(WRC_EVT_TRANSPORT_CLOSED, transport_id, 0);
  return rc;
}

iwrc rct_transport_dump(wrc_resource_t transport_id, JBL *dump_out) {
  return rct_resource_json_command(transport_id, WRC_CMD_TRANSPORT_DUMP, 0, 0, dump_out);
}

iwrc rct_transport_stats(wrc_resource_t transport_id, JBL *result_out) {
  return rct_resource_json_command(transport_id, WRC_CMD_TRANSPORT_GET_STATS, 0, 0, result_out);
}

iwrc rct_transport_set_max_incoming_bitrate(wrc_resource_t transport_id, uint32_t bitrate) {
  JBL jbl = 0;
  iwrc rc = jbl_create_empty_object(&jbl);
  RCGO(rc, finish);

  RCC(rc, finish, jbl_set_int64(jbl, "bitrate", bitrate));
  rc = rct_resource_json_command(transport_id, WRC_CMD_TRANSPORT_SET_MAX_INCOMING_BITRATE, 0, jbl, 0);
  if (!rc) {
    wrc_notify_event_handlers(WRC_EVT_TRANSPORT_UPDATED, transport_id, 0);
  }

finish:
  jbl_destroy(&jbl);
  return rc;
}

iwrc rct_transport_enable_trace_events(wrc_resource_t transport_id, uint32_t types) {
  JBL jbl = 0, jbl2 = 0;
  iwrc rc = jbl_create_empty_object(&jbl);
  RCGO(rc, finish);
  RCC(rc, finish, jbl_create_empty_array(&jbl2));

  if (types & RCT_TRANSPORT_TRACE_EVT_PROBATION) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "probation"));
  }
  if (types & RCT_TRANSPORT_TRACE_EVT_BWE) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "bwe"));
  }

  RCC(rc, finish, jbl_set_nested(jbl, "types", jbl2));
  rc = rct_resource_json_command(transport_id, WRC_CMD_TRANSPORT_ENABLE_TRACE_EVENT, 0, jbl, 0);

finish:
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  return rc;
}

iwrc rct_transport_connect(wrc_resource_t transport_id, rct_transport_connect_t *spec) {
  iwrc rc = 0;
  int type;
  bool locked = true;

  rct_transport_t *transport = rct_resource_by_id_locked(transport_id, RCT_TYPE_TRANSPORT_ALL, __func__);
  RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  type = transport->type;
  rct_resource_unlock_keep_ref(transport), locked = false;

  if ((spec->type != type) || ((type != RCT_TYPE_TRANSPORT_WEBRTC) && (type != RCT_TYPE_TRANSPORT_PLAIN))) {
    rc = IW_ERROR_INVALID_ARGS;
    iwlog_ecode_error3(rc);
    goto finish;
  }

  switch (type) {
    case RCT_TYPE_TRANSPORT_WEBRTC:
      rc = _rct_transport_webrtc_connect((void*) transport, &spec->wbrtc);
      break;
    case RCT_TYPE_TRANSPORT_PLAIN:
      rc = _rct_transport_plain_connect((void*) transport, &spec->plain);
      break;
    default:
      break;
  }

finish:
  rct_resource_ref_unlock(transport, locked, -1, __func__);
  iwpool_destroy(spec->pool);
  return rc;
}

iwrc rct_transport_complete_registration(JBL data, rct_transport_t *transport) {
  iwrc rc = 0, rc2;
  if (!data) {
    return 0;
  }
  rct_resource_ref_locked(transport, 1, __func__);

  JBL_NODE n;
  IWPOOL *pool = transport->pool;
  IWPOOL *pool_prev = iwpool_user_data_detach(pool);
  IWPOOL *pool_data = iwpool_create_empty();
  RCA(pool_data, finish);
  RCC(rc, finish, jbl_to_node(data, &transport->data, true, pool_data));

  rc2 = jbn_at(transport->data, "/data/sctpParameters/MIS", &n);
  if (!rc2 && (n->type == JBV_I64)) {
    uint64_t ms = n->vi64;
    if (ms != transport->stream_max_slots) {
      transport->stream_max_slots = ms;
      if ((transport->stream_max_slots > 4096) || (transport->stream_max_slots < 0)) {
        rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
        goto finish;
      }
      if (transport->stream_max_slots) {
        transport->stream_slots = iwpool_calloc(transport->stream_max_slots, transport->pool);
      }
    }
  }
  iwpool_destroy(pool_prev);
  iwpool_user_data_set(pool, pool_data, iwpool_free_fn);

finish:
  if (rc) {
    iwpool_destroy(pool_data);
    iwpool_user_data_detach(pool);
    iwpool_user_data_set(pool, pool_prev, iwpool_free_fn);
  } else {
    iwpool_destroy(pool_prev);
  }

  rct_resource_unlock(transport, __func__);
  return rc;
}

int rct_transport_next_mid_lk(rct_transport_t *transport) {
  if (transport->next_mid == 100000000) {
    iwlog_error("RTC transport: reaching max MID value: %d", transport->next_mid);
    transport->next_mid = 0;
  }
  return transport->next_mid++;
}

int rct_transport_next_id_lk(rct_transport_t *transport) {
  if ((transport->stream_max_slots < 1) || !transport->stream_slots) {
    return 0;
  }
  int id = -1;
  for (int i = 0, l = transport->stream_max_slots; i < l; ++i) {
    id = (transport->stream_next_id + i) % l;
    if (!transport->stream_slots[i]) {
      transport->stream_slots[id] = true;
      transport->stream_next_id = id + 1;
      return id;
    }
  }
  return -1;
}

static iwrc _rct_connect_webrtc_spec_from_json(
  JBL_NODE json, JBL_NODE n, rct_transport_connect_t *spec,
  IWPOOL *pool
  ) {
  iwrc rc = 0;
  JBL_NODE n2;

  for (JBL_NODE fpn = n->child; fpn; fpn = fpn->next) {
    if (fpn->type != JBV_OBJECT) {
      rc = GR_ERROR_INVALID_DATA_RECEIVED;
      goto finish;
    }
    RCC(rc, finish, jbn_at(fpn, "/algorithm", &n));
    if (n->type != JBV_STR) {
      rc = GR_ERROR_INVALID_DATA_RECEIVED;
      goto finish;
    }
    RCC(rc, finish, jbn_at(fpn, "/value", &n2));
    if (n->type != JBV_STR) {
      rc = GR_ERROR_INVALID_DATA_RECEIVED;
      goto finish;
    }
    rct_hash_func_e algorithm = rct_name_to_hash_func(n->vptr);
    if (!algorithm) {
      rc = GR_ERROR_INVALID_DATA_RECEIVED;
      goto finish;
    }
    struct rct_dtls_fp *fp = iwpool_alloc(sizeof(*fp), pool);
    RCA(fp, finish);
    fp->algorithm = algorithm;
    fp->value = iwpool_strndup(pool, n2->vptr, n2->vsize, &rc);
    RCGO(rc, finish);
    fp->next = spec->wbrtc.fingerprints;
    spec->wbrtc.fingerprints = fp;
  }

  rc = jbn_at(json, "/role", &n2);
  if (rc || (n2->type != JBV_STR)) {
    rc = 0;
    spec->wbrtc.role = 0;
  } else {
    spec->wbrtc.role = iwpool_strndup(pool, n2->vptr, n2->vsize, &rc);
    RCGO(rc, finish);
  }

finish:
  return rc;
}

iwrc rct_transport_connect_spec_from_json(JBL_NODE json, rct_transport_connect_t **spec_out) {
  *spec_out = 0;

  JBL_NODE n;
  iwrc rc = 0;
  rct_transport_connect_t *spec = 0;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCB(finish, spec = iwpool_calloc(sizeof(*spec), pool));
  spec->pool = pool;

  rc = jbn_at(json, "/fingerprints", &n);
  if (!rc) {
    if (n->type != JBV_ARRAY) {
      rc = GR_ERROR_INVALID_DATA_RECEIVED;
      goto finish;
    }
    rc = _rct_connect_webrtc_spec_from_json(json, n, spec, pool);
    spec->type = RCT_TYPE_TRANSPORT_WEBRTC;
  } else {
    // todo: implement for plain transport
    spec->type = RCT_TYPE_TRANSPORT_PLAIN;
    rc = IW_ERROR_NOT_IMPLEMENTED;
    iwlog_ecode_error3(rc);
    goto finish;
  }

finish:
  if (rc) {
    iwpool_destroy(pool);
  } else {
    *spec_out = spec;
  }
  return rc;
}
