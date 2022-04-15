#include "rct_transport.h"

iwrc rct_transport_webrtc_spec_create2(
  uint32_t flags,
  const char *listen_ip, const char *announced_ip,
  rct_transport_webrtc_spec_t **spec_out
  ) {
  IWPOOL *pool = iwpool_create(512);
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  *spec_out = iwpool_calloc(sizeof(**spec_out), pool);
  if (!*spec_out) {
    iwpool_destroy(pool);
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  (*spec_out)->pool = pool;
  (*spec_out)->flags |= flags;
  if (listen_ip) {
    iwrc rc = rct_transport_webrtc_spec_add_ip(*spec_out, listen_ip, announced_ip);
    if (rc) {
      *spec_out = 0;
      iwpool_destroy(pool);
      return rc;
    }
  }
  return 0;
}

iwrc rct_transport_webrtc_spec_create(uint32_t flags, rct_transport_webrtc_spec_t **spec_out) {
  return rct_transport_webrtc_spec_create2(flags, 0, 0, spec_out);
}

iwrc rct_transport_webrtc_spec_add_ip(rct_transport_webrtc_spec_t *spec, const char *_ip, const char *announced_ip) {
  if (!spec->pool || !_ip) {
    return IW_ERROR_INVALID_ARGS;
  }
  const char *ip = _ip;
  if (strcmp(ip, "auto") == 0) {
    ip = g_env.auto_ip;
    if (!ip) {
      iwlog_warn2("Failed to detect host IP address");
      return 0;
    } else if (g_env.log.verbose) {
      iwlog_info("Using auto detected host IP address: %s", ip);
    }
  }
  iwrc rc = 0;
  IWPOOL *pool = spec->pool;
  rct_transport_ip_t *n = iwpool_alloc(sizeof(*n), pool);
  RCA(n, finish);
  n->announced_ip = 0;
  n->ip = iwpool_strdup(pool, ip, &rc);
  RCGO(rc, finish);
  if (announced_ip) {
    n->announced_ip = iwpool_strdup(pool, announced_ip, &rc);
    RCGO(rc, finish);
  }
  rct_transport_ip_t *next = spec->ips;
  spec->ips = n;
  n->next = next;

finish:
  return rc;
}

static iwrc _rct_transport_webrtc_create_data(rct_transport_webrtc_spec_t *spec, JBL jbl_data) {
  iwrc rc = 0;
  JBL jbl = 0, jbl2 = 0;
  rct_transport_ip_t *ip = spec->ips;

  RCC(rc, finish, jbl_create_empty_array(&jbl));
  while (ip) {
    jbl_destroy(&jbl2);
    RCC(rc, finish, jbl_create_empty_object(&jbl2));
    RCC(rc, finish, jbl_set_string(jbl2, "ip", ip->ip));
    if (ip->announced_ip) {
      RCC(rc, finish, jbl_set_string(jbl2, "announcedIp", ip->announced_ip));
    }
    RCC(rc, finish, jbl_set_nested(jbl, 0, jbl2));
    ip = ip->next;
  }
  RCC(rc, finish, jbl_set_nested(jbl_data, "listenIps", jbl));
  if (!spec->flags) {
    spec->flags = RCT_TRN_ENABLE_UDP | RCT_TRN_PREFER_UDP | RCT_TRN_ENABLE_TCP | RCT_TRN_ENABLE_DATA_CHANNEL;
  }
  RCC(rc, finish, jbl_set_bool(jbl_data, "enableUdp", spec->flags & RCT_TRN_ENABLE_UDP));
  RCC(rc, finish, jbl_set_bool(jbl_data, "enableTcp", spec->flags & RCT_TRN_ENABLE_TCP));
  RCC(rc, finish, jbl_set_bool(jbl_data, "preferUdp", spec->flags & RCT_TRN_PREFER_UDP));
  RCC(rc, finish, jbl_set_bool(jbl_data, "preferTcp", spec->flags & RCT_TRN_PREFER_TCP));
  RCC(rc, finish, jbl_set_bool(jbl_data, "enableSctp", spec->flags & RCT_TRN_ENABLE_SCTP));
  RCC(rc, finish, jbl_set_bool(jbl_data, "isDataChannel", spec->flags & RCT_TRN_ENABLE_DATA_CHANNEL));
  if (!spec->initial.outgoing_bitrate) {
    spec->initial.outgoing_bitrate = 600000;
  }
  RCC(rc, finish, jbl_set_int64(jbl_data, "initialAvailableOutgoingBitrate", spec->initial.outgoing_bitrate));

  jbl_destroy(&jbl);
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  if (!spec->sctp.streams.os) {
    spec->sctp.streams.os = 1024;
  }
  RCC(rc, finish, jbl_set_int64(jbl, "OS", spec->sctp.streams.os));
  if (!spec->sctp.streams.mis) {
    spec->sctp.streams.mis = 1024;
  }
  RCC(rc, finish, jbl_set_int64(jbl, "MIS", spec->sctp.streams.mis));
  RCC(rc, finish, jbl_set_nested(jbl_data, "numSctpStreams", jbl));
  if (!spec->sctp.max_message_size) {
    spec->sctp.max_message_size = 262144;
  }
  RCC(rc, finish, jbl_set_int64(jbl_data, "maxSctpMessageSize", spec->sctp.max_message_size));

finish:
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  return rc;
}

iwrc rct_transport_webrtc_create(
  rct_transport_webrtc_spec_t *spec, wrc_resource_t router_id,
  wrc_resource_t *transport_id_out
  ) {
  if (!spec || !transport_id_out) {
    return IW_ERROR_INVALID_ARGS;
  }
  *transport_id_out = 0;

  iwrc rc = 0;
  IWPOOL *pool = spec->pool;
  bool locked = false;
  rct_router_t *router = 0;
  rct_transport_webrtc_t *transport = 0;
  wrc_msg_t *m = 0;

  if (!spec->ips) {
    for (struct gr_server *s = g_env.servers; s; s = s->next) {
      if (s->type == GR_LISTEN_ANNOUNCED_IP_TYPE) {
        const char *listen_ip = s->user;
        const char *announced_ip = 0;
        if (listen_ip) {
          announced_ip = s->host;
        } else {
          listen_ip = s->host;
        }
        if (listen_ip) {
          RCC(rc, finish, rct_transport_webrtc_spec_add_ip(spec, listen_ip, announced_ip));
        }
      }
    }
  }
  if (!spec->ips) {
    RCC(rc, finish, rct_transport_webrtc_spec_add_ip(spec, "127.0.0.1", 0));
  }

  RCB(finish, transport = iwpool_calloc(sizeof(*transport), pool));
  transport->pool = pool;
  transport->type = RCT_TYPE_TRANSPORT_WEBRTC;
  transport->spec = spec;
  spec = 0;
  iwu_uuid4_fill(transport->uuid);

  router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__), locked = true;
  RCIF(!router, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  rct_resource_ref_lk(transport, RCT_INIT_REFS, __func__);
  transport->router = rct_resource_ref_lk(router, 1, __func__);

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_ROUTER_CREATE_WEBRTC_TRANSPORT
      }
    }
  }));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  RCC(rc, finish, _rct_transport_webrtc_create_data(transport->spec, m->input.worker.data));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "routerId", router->uuid));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "transportId", transport->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &transport->identity, pool));

  rct_transport_t *th = router->transports;
  router->transports = (void*) transport;
  transport->next = th;

  transport->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(transport));
  rct_resource_unlock_keep_ref(router), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));
  RCC(rc, finish, rct_transport_complete_registration(m->output.worker.data, (void*) transport));

  wrc_notify_event_handlers(WRC_EVT_TRANSPORT_CREATED, transport->id, 0);
  *transport_id_out = transport->id;

finish:
  rct_resource_ref_keep_locking(transport, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(router, locked, -1, __func__);
  wrc_msg_destroy(m);
  if (spec) {
    iwpool_destroy(spec->pool);
  }
  return rc;
}

iwrc rct_transport_webrtc_restart_ice(wrc_resource_t transport_id, JBL *result_out) {
  return rct_resource_json_command(transport_id, WRC_CMD_TRANSPORT_RESTART_ICE,
                                   RCT_TYPE_TRANSPORT_WEBRTC, 0, result_out);
}

iwrc _rct_transport_webrtc_connect(rct_transport_webrtc_t *transport, rtc_transport_webrtc_connect_t *spec) {
  JBL jbl = 0, jbl2 = 0, jbl3 = 0;
  wrc_msg_t *m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = transport->router->worker_id,
    .input = {
      .worker     = {
        .cmd      = WRC_CMD_TRANSPORT_CONNECT,
        .internal = transport->identity
      }
    }
  });
  if (!m) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  iwrc rc = jbl_create_empty_object(&jbl);
  RCGO(rc, finish);
  if (spec->role) {
    RCC(rc, finish, jbl_set_string(jbl, "role", spec->role));
  }
  RCC(rc, finish, jbl_create_empty_array(&jbl2));
  for (struct rct_dtls_fp *fp = spec->fingerprints; fp; fp = fp->next) {
    jbl_destroy(&jbl3);
    RCC(rc, finish, jbl_create_empty_object(&jbl3));
    RCC(rc, finish, jbl_set_string(jbl3, "algorithm", rct_hash_func_name(fp->algorithm)));
    RCC(rc, finish, jbl_set_string(jbl3, "value", fp->value));
    RCC(rc, finish, jbl_set_nested(jbl2, 0, jbl3));
  }
  RCC(rc, finish, jbl_set_nested(jbl, "fingerprints", jbl2));
  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  RCC(rc, finish, jbl_set_nested(m->input.worker.data, "dtlsParameters", jbl));

  rc = wrc_send_and_wait(m, 0);

finish:
  if (m) {
    m->input.worker.internal = 0; // identity will be freed by caller
    wrc_msg_destroy(m);
  }
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  jbl_destroy(&jbl3);
  return rc;
}
