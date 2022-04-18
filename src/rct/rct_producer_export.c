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

#include "rct_producer_export.h"
#include "rct_consumer.h"
#include "utils/network.h"

#include <ejdb2/iowow/iwstw.h>
#include <assert.h>

static void _rct_producer_export_dispose_lk(rct_producer_export_t *export) {
  rct_resource_ref_lk(export->consumer, -1, __func__);
  rct_resource_ref_lk(export->transport, -1, __func__);
  rct_resource_ref_lk(export->producer, -1, __func__);
}

static void _rct_producer_export_close_task(void *arg) {
  rct_producer_export_t *export = arg;
  assert(export);
  if (export->on_close) {
    export->on_close(export);
    export->on_close = 0;
  }
  wrc_resource_t transport_id = 0;
  rct_lock();
  transport_id = export->transport ? export->transport->id : 0;
  rct_unlock();
  if (transport_id) {
    rct_transport_close_async(transport_id);
  }
  // Release the keeping ref
  rct_resource_ref_unlock(export, false, -1, __func__);
}

static void _rct_producer_export_close_lk(rct_producer_export_t *export) {
  if (export->producer && export->producer->export == export) {
    export->producer->export = 0;
  }
  if (export->consumer && export->consumer->export == export) {
    export->consumer->export = 0;
  }
  if (export->on_close) {
    // +1 to main ref in order to keep it _rct_producer_export_close_task()
    rct_resource_ref_lk(export, 1, __func__);
    iwrc rc = iwtp_schedule(g_env.tp, _rct_producer_export_close_task, export);
    if (rc) {
      rct_resource_ref_lk(export, -1, __func__); // Also release the keeping ref
      iwlog_ecode_error3(rc);
    }
  }
}

void rct_producer_export_close(wrc_resource_t export_id) {
  rct_resource_close(export_id);
}

iwrc rct_export_consumer_pause(wrc_resource_t export_id) {
  wrc_resource_t consumer_id = 0;
  rct_producer_export_t *export = rct_resource_by_id_locked(export_id, RCT_TYPE_PRODUCER_EXPORT, __func__);
  if (export) {
    consumer_id = export->consumer ? export->consumer->id : 0;
  }
  rct_resource_unlock(export, __func__);
  if (consumer_id) {
    return rct_consumer_pause(consumer_id);
  } else {
    return 0;
  }
}

iwrc rct_export_consumer_resume(wrc_resource_t export_id) {
  wrc_resource_t id = 0;
  rct_producer_export_t *export = rct_resource_by_id_locked(export_id, RCT_TYPE_PRODUCER_EXPORT, __func__);
  if (export) {
    id = export->consumer ? export->consumer->id : 0;
  }
  rct_resource_unlock(export, __func__);
  if (id) {
    return rct_consumer_resume(id);
  } else {
    return 0;
  }
}

static iwrc _export_consumer_create_sdp(rct_producer_export_t *export) {
  iwrc rc = 0;
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  char *codec_name = 0;
  int payload_type = 0, clock_rate = 0, channels = 2;
  int rtp_kind = export->producer->spec->rtp_kind;
  JBL_NODE codec = export->codec, n;

  RCC(rc, finish, jbn_at(codec, "/payloadType", &n));
  if (n->type != JBV_I64) {
    rc = IW_ERROR_INVALID_VALUE;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  payload_type = n->vi64;

  RCC(rc, finish, jbn_at(codec, "/mimeType", &n));
  if (n->type != JBV_STR) {
    rc = IW_ERROR_INVALID_VALUE;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  codec_name = strchr(n->vptr, '/');
  if (codec_name == 0 || *(codec_name + 1) == '\0') {
    rc = IW_ERROR_INVALID_VALUE;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  codec_name++;

  RCC(rc, finish, jbn_at(codec, "/clockRate", &n));
  if (n->type != JBV_I64) {
    rc = IW_ERROR_INVALID_VALUE;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  clock_rate = n->vi64;

  if (rtp_kind == RTP_KIND_AUDIO) {
    rc = jbn_at(codec, "/channels", &n);
    if (!rc && n->type == JBV_I64) {
      channels = n->vi64;
    }
  }

  RCC(rc, finish,
      iwxstr_cat2(xstr,
                  "v=0\r\n"
                  "o=- 0 0 IN IP4 127.0.0.1\r\n"
                  "s=Wirow\r\n"
                  "c=IN IP4 127.0.0.1\r\n"
                  "t=0 0\r\n"
                  ));

  RCC(rc, finish,
      iwxstr_printf(xstr,
                    "m=%s %d RTP/AVP %d\r\n",
                    rtp_kind == RTP_KIND_VIDEO ? "video" : "audio",
                    export->port, payload_type));

  if (rtp_kind == RTP_KIND_AUDIO) {
    RCC(rc, finish,
        iwxstr_printf(xstr,
                      "a=rtpmap:%d %s/%d/%d\r\n",
                      payload_type, codec_name, clock_rate, channels));
  } else {
    RCC(rc, finish,
        iwxstr_printf(xstr,
                      "a=rtpmap:%d %s/%d\r\n",
                      payload_type, codec_name, clock_rate));
  }

  RCC(rc, finish, iwxstr_cat2(xstr, "a=sendonly\r\n"));

  export->sdp = iwpool_strdup(export->pool, iwxstr_ptr(xstr), &rc);

finish:
  iwxstr_destroy(xstr);
  return rc;
}

static iwrc _export_consumer_create(rct_producer_export_t *export) {
  iwrc rc = 0;
  wrc_resource_t consumer_id;
  rct_producer_t *producer = export->producer;
  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    rc = iwrc_set_errno(IW_ERROR_ALLOC, errno);
    return rc;
  }
  JBL_NODE consumer_caps, consumer_codecs, codecs;
  JBL_NODE consumable_rtp_parameters = producer->spec->consumable_rtp_parameters;
  RCC(rc, finish, jbn_from_json("{\"codecs\":[], \"rtcpFeedback\":[]}", &consumer_caps, pool));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/codecs", &codecs));
  RCC(rc, finish, jbn_at(consumer_caps, "/codecs", &consumer_codecs));

  for (JBL_NODE c = codecs->child; c; c = c->next) {
    if (rct_codec_is_rtx(c)) {
      continue;
    }
    // Keep codec object in export struct
    RCC(rc, finish, jbn_clone(c, &export->codec, export->pool));
    break;
  }
  if (!export->codec) {
    rc = RCT_ERROR_UNSUPPORTED_CODEC;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  jbn_add_item(consumer_codecs, export->codec);

  RCC(rc,
      finish,
      rct_consumer_create2(export->transport->id, export->producer->id, consumer_caps, true, 0, &consumer_id));

  rct_consumer_t *consumer = rct_resource_by_id_locked(consumer_id, RCT_TYPE_CONSUMER, __func__);
  if (consumer) {
    consumer->resume_by_producer = true;
    consumer->export = export;
    export->consumer = rct_resource_ref_lk(consumer, 1, __func__);
  }
  rct_resource_unlock(consumer, __func__);

  export->codec->parent = 0;
  export->codec->next = 0;
  export->codec->prev = 0;

finish:
  iwpool_destroy(pool);
  return rc;
}

iwrc rct_producer_export(
  wrc_resource_t                      producer_id,
  const rct_producer_export_params_t *params,
  wrc_resource_t                     *out_export_id) {

  *out_export_id = 0;

  iwrc rc = 0;
  wrc_resource_t transport_id, router_id;
  rct_producer_t *producer = 0;
  rct_transport_t *transport = 0;
  rct_producer_export_t *export = 0;
  bool locked = false;
  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  export = iwpool_calloc(sizeof(*export), pool);
  if (!export) {
    rc = iwrc_set_errno(IW_ERROR_ALLOC, errno);
    iwpool_destroy(pool);
    return rc;
  }

  export->pool = pool;
  export->type = RCT_TYPE_PRODUCER_EXPORT;
  rct_resource_ref_lk(export, RCT_INIT_REFS, __func__);
  iwu_uuid4_fill(export->uuid);
  export->dispose = (void*) _rct_producer_export_dispose_lk;
  export->close = (void*) _rct_producer_export_close_lk;

  producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER, __func__), locked = true;
  RCIF(!producer, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);
  router_id = producer->transport->router->id;
  if (producer->export) {
    rct_resource_close_lk(producer->export);
  }
  export->producer = rct_resource_ref_lk(producer, 1, __func__);
  producer->export = export;
  rct_unlock(), locked = false;

  // Create export transport
  {
    rct_transport_plain_spec_t *spec;
    RCC(rc, finish, rct_transport_plain_spec_create("127.0.0.1", 0, &spec));
    spec->comedia = false;
    spec->no_mux = false;

    RCC(rc, finish, rct_transport_plain_create(spec, router_id, &transport_id));
    transport = rct_resource_by_id_locked(transport_id, RCT_TYPE_TRANSPORT_PLAIN, __func__), locked = true;
    RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);
    export->transport = rct_resource_ref_lk(transport, 1, __func__);
    rct_resource_unlock(transport, __func__), locked = false;
  }

  // Connect transport
  {
    export->port = network_detect_unused_port("127.0.0.1");
    if (!export->port) {
      rc = IW_ERROR_FAIL;
      iwlog_ecode_error2(rc, "Failed to get unused port for 127.0.0.1");
      goto finish;
    }
    rct_transport_connect_t *spec;
    RCC(rc, finish, rct_transport_plain_connect_spec_create("127.0.0.1", 0, &spec));
    spec->type = RCT_TYPE_TRANSPORT_PLAIN;
    spec->plain.port = export->port;
    RCC(rc, finish, rct_transport_connect(export->transport->id, spec));
  }

  // Create export consumer in paused state
  RCC(rc, finish, _export_consumer_create(export));
  RCC(rc, finish, _export_consumer_create_sdp(export));

  rct_lock(), locked = true;
  export->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(export));

  export->hook_user_data = params->hook_user_data;
  export->on_start = params->on_start;
  export->on_resume = params->on_resume;
  export->on_pause = params->on_pause;
  export->on_close = params->on_close;
  export->close_on_pause = params->close_on_pause;
  rct_unlock(), locked = false;

  if (export->on_start) {
    RCC(rc, finish, export->on_start(export));
  } else {
    rct_export_consumer_resume(export->id);
  }

finish:
  rct_resource_ref_keep_locking(export, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(producer, locked, -1, __func__);
  return rc;
}

///////////////////////////////////////////////////////////////////////////
//								            Module                                     //
///////////////////////////////////////////////////////////////////////////

static iwrc _rct_event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  iwrc rc = 0;
  switch (evt) {
    case WRC_EVT_CONSUMER_PAUSE: {
      rct_consumer_t *consumer = rct_resource_by_id_locked(resource_id, RCT_TYPE_CONSUMER, __func__);
      if (consumer->export && consumer->export->close_on_pause) {
        rct_resource_close_lk(consumer->export);
        rct_resource_unlock(consumer, __func__);
      } else if (consumer->export && consumer->export->on_pause) {
        rct_producer_export_t *export = consumer->export;
        rct_resource_ref_lk(export, 1, __func__);
        rct_resource_unlock(consumer, __func__);
        export->on_pause(export);
        rct_resource_ref_unlock(export, false, -1, __func__);
      } else {
        rct_resource_unlock(consumer, __func__);
      }
      break;
    }
    case WRC_EVT_CONSUMER_RESUME: {
      rct_consumer_t *consumer = rct_resource_by_id_locked(resource_id, RCT_TYPE_CONSUMER, __func__);
      if (consumer->export && consumer->export->on_resume) {
        rct_producer_export_t *export = consumer->export;
        rct_resource_ref_lk(export, 1, __func__);
        rct_resource_unlock(consumer, __func__);
        export->on_resume(export);
        rct_resource_ref_unlock(export, false, -1, __func__);
      } else {
        rct_resource_unlock(consumer, __func__);
      }
      break;
    }
    default:
      break;
  }
  return rc;
}

static uint32_t _event_handler_id;

iwrc rct_producer_export_module_init(void) {
  return wrc_add_event_handler(_rct_event_handler, 0, &_event_handler_id);
}

void rct_producer_export_module_close(void) {
  wrc_remove_event_handler(_event_handler_id);
}
