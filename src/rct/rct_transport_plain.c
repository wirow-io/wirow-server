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

#include <string.h>

iwrc rct_transport_plain_connect_spec_create(
  const char               *ip,
  const char               *key_base64,
  rct_transport_connect_t **spec_out
  ) {
  rct_transport_connect_t *spec;
  iwrc rc = 0;
  size_t len_ip = ip ? strlen(ip) : 0;
  size_t len_key_base64 = key_base64 ? strlen(key_base64) : 0;
  IWPOOL *pool = iwpool_create(sizeof(*spec) + len_ip + len_key_base64);
  if (!pool) {
    *spec_out = 0;
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  RCB(finish, spec = iwpool_calloc(sizeof(*spec), pool));
  spec->pool = pool;

  if (ip) {
    spec->plain.ip = iwpool_strndup(pool, ip, len_ip, &rc);
    RCGO(rc, finish);
  }
  if (key_base64) {
    spec->plain.key_base64 = iwpool_strndup(pool, key_base64, len_key_base64, &rc);
  }

finish:
  if (rc) {
    *spec_out = 0;
    iwpool_destroy(pool);
  } else {
    *spec_out = spec;
  }
  return rc;
}

iwrc _rct_transport_plain_connect(rct_transport_plain_t *transport, rct_transport_plain_connect_t *spec) {
  iwrc rc = 0;
  JBL jbl, jbl2 = 0;
  wrc_msg_t *m;

  RCB(finish,  m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = transport->router->worker_id,
    .input = {
      .worker     = {
        .cmd      = WRC_CMD_TRANSPORT_CONNECT,
        .internal = transport->identity
      }
    }
  }));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  jbl = m->input.worker.data;

  if (spec->ip) {
    RCC(rc, finish, jbl_set_string(jbl, "ip", spec->ip));
  }
  if (spec->port > 0) {
    RCC(rc, finish, jbl_set_int64(jbl, "port", spec->port));
  }
  if (spec->rtcp_port > 0) {
    RCC(rc, finish, jbl_set_int64(jbl, "rtcpPort", spec->rtcp_port));
  }
  if (spec->key_base64) {
    RCC(rc, finish, jbl_create_empty_object(&jbl2));
    switch (spec->crypto_suite) {
      case RCT_AES_CM_128_HMAC_SHA1_80:
        RCC(rc, finish, jbl_set_string(jbl2, "cryptoSuite", "AES_CM_128_HMAC_SHA1_80"));
        break;
      case RCT_AES_CM_128_HMAC_SHA1_32:
        RCC(rc, finish, jbl_set_string(jbl2, "cryptoSuite", "AES_CM_128_HMAC_SHA1_32"));
        break;
    }
    RCC(rc, finish, jbl_set_string(jbl2, "keyBase64", spec->key_base64));
    jbl_set_nested(jbl, "srtpParameters", jbl2);
  }

  RCC(rc, finish, wrc_send_and_wait(m, 0));
  if (!m->output.worker.data) {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  {
    rct_resource_ref_locked(transport, 1, __func__);
    JBL_NODE data, n;
    IWPOOL *pool = transport->pool;
    IWPOOL *pool_prev = iwpool_user_data_detach(pool);
    IWPOOL *pool_data = iwpool_create(iwpool_allocated_size(pool_prev));
    RCA(pool_data, finish_data);

    RCC(rc, finish_data, jbl_to_node(m->output.worker.data, &n, true, pool_prev));
    RCC(rc, finish_data, jbn_clone(transport->data, &data, pool_data));
    RCC(rc, finish_data, jbn_copy_paths(n, data, (const char*[]) {
      "/data/srtpParameters", "/data/tuple", "/data/srtpParameters", 0
    }, true, false, pool_data));

    // Update transport data
    transport->data = data;
    iwpool_user_data_set(pool, pool_data, iwpool_free_fn);

finish_data:
    if (rc) {
      iwpool_destroy(pool_data);
      iwpool_user_data_detach(pool);
      iwpool_user_data_set(pool, pool_prev, iwpool_free_fn);
    } else {
      iwpool_destroy(pool_prev);
    }
    rct_resource_unlock(transport, __func__);
  }

finish:
  if (m) {
    m->input.worker.internal = 0; // identity will be freed by caller
    wrc_msg_destroy(m);
  }
  jbl_destroy(&jbl2);
  return rc;
}

iwrc rct_transport_plain_spec_create(
  const char                  *listen_ip,
  const char                  *announced_ip,
  rct_transport_plain_spec_t **spec_out
  ) {
  *spec_out = 0;

  if (!listen_ip) {
    iwlog_error2("listen_ip is not set");
    return IW_ERROR_INVALID_ARGS;
  }

  iwrc rc = 0;
  IWPOOL *pool = iwpool_create(sizeof(**spec_out) + (listen_ip ? strlen(listen_ip) * 4 : 0));
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  rct_transport_plain_spec_t *spec = iwpool_calloc(sizeof(*spec), pool);
  RCA(spec, finish);
  spec->pool = pool;
  spec->listen_ip.ip = iwpool_strdup(pool, listen_ip, &rc);
  RCGO(rc, finish);

  if (announced_ip) {
    spec->listen_ip.announced_ip = iwpool_strdup(pool, announced_ip, &rc);
    RCGO(rc, finish);
  }

finish:
  if (rc) {
    iwpool_destroy(pool);
  } else {
    *spec_out = spec;
  }
  return rc;
}

static iwrc _rct_transport_plain_create_data(rct_transport_plain_spec_t *spec, JBL jbl_data) {
  iwrc rc = 0;
  JBL jbl = 0;

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "ip", spec->listen_ip.ip));
  if (spec->listen_ip.announced_ip) {
    RCC(rc, finish, jbl_set_string(jbl, "announcedIp", spec->listen_ip.announced_ip));
  }
  RCC(rc, finish, jbl_set_nested(jbl_data, "listenIp", jbl));

  RCC(rc, finish, jbl_set_bool(jbl_data, "rtcpMux", !spec->no_mux));
  RCC(rc, finish, jbl_set_bool(jbl_data, "comedia", spec->comedia));
  RCC(rc, finish, jbl_set_bool(jbl_data, "enableSctp", spec->enable_sctp));


  if (spec->sctp.streams.os < 1) {
    spec->sctp.streams.os = 1024;
  }
  if (spec->sctp.streams.mis < 1) {
    spec->sctp.streams.mis = 1024;
  }
  if (spec->sctp.max_message_size < 1) {
    spec->sctp.max_message_size = 262144;
  }

  jbl_destroy(&jbl);
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_int64(jbl, "OS", spec->sctp.streams.os));
  RCC(rc, finish, jbl_set_int64(jbl, "MIS", spec->sctp.streams.mis));
  RCC(rc, finish, jbl_set_nested(jbl_data, "numSctpStreams", jbl));

  RCC(rc, finish, jbl_set_int64(jbl_data, "maxSctpMessageSize", spec->sctp.max_message_size));
  RCC(rc, finish, jbl_set_bool(jbl_data, "isDataChannel", false));
  RCC(rc, finish, jbl_set_bool(jbl_data, "enableSrtp", spec->enable_srtp));

  switch (spec->crypto_suite) {
    case RCT_AES_CM_128_HMAC_SHA1_32:
      RCC(rc, finish, jbl_set_string(jbl_data, "srtpCryptoSuite", "AES_CM_128_HMAC_SHA1_32"));
      break;
    default:
      RCC(rc, finish, jbl_set_string(jbl_data, "srtpCryptoSuite", "AES_CM_128_HMAC_SHA1_80"));
  }

finish:
  jbl_destroy(&jbl);
  return rc;
}

iwrc rct_transport_plain_create(
  rct_transport_plain_spec_t *spec,
  wrc_resource_t              router_id,
  wrc_resource_t             *transport_id_out
  ) {
  if (!spec || !transport_id_out) {
    return IW_ERROR_INVALID_ARGS;
  }
  *transport_id_out = 0;

  iwrc rc = 0;
  IWPOOL *pool = spec->pool;
  bool locked = false;
  rct_router_t *router = 0;
  rct_transport_plain_t *transport = 0;
  wrc_msg_t *m = 0;

  RCB(finish, transport = iwpool_calloc(sizeof(*transport), pool));
  transport->pool = pool;
  transport->type = RCT_TYPE_TRANSPORT_PLAIN;
  transport->spec = spec;
  iwu_uuid4_fill(transport->uuid);

  router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__), locked = true;
  RCIF(!router, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  rct_resource_ref_lk(transport, RCT_INIT_REFS, __func__);
  transport->router = rct_resource_ref_lk(router, 1, __func__);

  m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_ROUTER_CREATE_PLAIN_TRANSPORT
      }
    }
  });
  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  RCC(rc, finish, _rct_transport_plain_create_data(spec, m->input.worker.data));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "routerId", router->uuid));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "transportId", transport->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &transport->identity, pool));

  rct_transport_t *th = router->transports;
  router->transports = (void*) transport;
  transport->next = th;

  rct_resource_register_lk(transport);
  rct_resource_unlock_keep_ref(router), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  if (m->output.worker.data) {
    RCC(rc, finish, rct_transport_complete_registration(m->output.worker.data, (void*) transport));
  } else {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  wrc_notify_event_handlers(WRC_EVT_TRANSPORT_CREATED, transport->id, 0);
  *transport_id_out = transport->id;

finish:
  rct_resource_ref_keep_locking(transport, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(router, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}
