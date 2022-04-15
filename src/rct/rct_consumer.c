#include "rct_consumer.h"

#include <iowow/iwutils.h>
#include <iowow/iwconv.h>


#define REPORT(msg_)                         \
  iwlog_error2(msg_);                        \
  return RCT_ERROR_INVALID_RTP_PARAMETERS;

void rct_consumer_dispose_lk(rct_consumer_base_t *consumer) {
  assert(consumer);
  if (consumer->producer) {
    for (rct_consumer_base_t *p = consumer->producer->consumers, *pp = 0; p; p = p->next) {
      if (p->id == consumer->id) {
        if (pp) {
          pp->next = p->next;
        } else {
          consumer->producer->consumers = p->next;
        }
        break;
      } else {
        pp = p;
      }
    }
  }

  if (consumer->transport) {
    for (rct_consumer_base_t *p = consumer->transport->consumers, *pp = 0; p; p = p->next_transport) {
      if (p->id == consumer->id) {
        if (pp) {
          pp->next_transport = p->next_transport;
        } else {
          consumer->transport->consumers = p->next_transport;
        }
        break;
      } else {
        pp = p;
      }
    }
  }

  switch (consumer->type) {
    case RCT_TYPE_CONSUMER:
      jbl_destroy(&((rct_consumer_t*) consumer)->producer_scores);
      break;
    case RCT_TYPE_CONSUMER_DATA:
      // todo:
      break;
  }

  // Unref parent consumer
  rct_resource_ref_lk(consumer->producer, -1, __func__);
  // Unref own transport
  rct_resource_ref_lk(consumer->transport, -1, __func__);
}

iwrc rct_consumer_close(wrc_resource_t consumer_id) {
  iwrc rc = rct_resource_json_command(consumer_id, WRC_CMD_CONSUMER_CLOSE, RCT_TYPE_CONSUMER, 0, 0);
  wrc_notify_event_handlers(WRC_EVT_CONSUMER_CLOSED, consumer_id, 0);
  return rc;
}

static iwrc _rct_validate_rtp_codec_capability(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("codec is not an object");
  }
  iwrc rc = jbn_at(n, "/mimeType", &r);
  if (rc || (r->type != JBV_STR)) {
    REPORT("missing codec.mimeType");
  }
  bool audio = false;
  char *p = utf8casestr(r->vptr, "video/");
  if (p != r->vptr) {
    p = utf8casestr(r->vptr, "audio/");
    if (p != r->vptr) {
      REPORT("invalid codec.mimeType");
    }
    audio = true;
  }

  // Just override kind with media component of mimeType.
  RCR(jbn_copy_path(&(struct _JBL_NODE) {
    .type = JBV_STR,
    .vptr = audio ? "audio" : "video",
    .vsize = 5
  }, "/", n, "/kind", false, false, pool));

  // preferredPayloadType is optional.
  rc = jbn_at(n, "/preferredPayloadType", &r);
  if (!rc && (r->type != JBV_I64)) {
    REPORT("invalid codec.preferredPayloadType");
  }

  // clockRate is mandatory.
  rc = jbn_at(n, "/clockRate", &r);
  if (rc || (r->type != JBV_I64)) {
    REPORT("missing codec.clockRate")
  }

  // channels is optional. If unset, set it to 1 (just if audio).
  rc = jbn_at(n, "/channels", &r);
  if (audio) {
    if (rc) {
      rc = RCR(jbn_add_item_i64(n, "channels", 1, &r, pool));
    } else if (r->type != JBV_I64) {
      r->type = JBV_I64;
      r->vi64 = 1;
      r->child = 0;
    }
  } else if (!rc) {
    jbn_detach(n, "/channels");
  }

  // parameters is optional. If unset, set it to an empty object.
  rc = jbn_at(n, "/parameters", &r);
  if (rc) {
    rc = RCR(jbn_add_item_obj(n, "parameters", &r, pool));
  } else if (r->type != JBV_OBJECT) {
    r->type = JBV_OBJECT;
    r->child = 0;
  }
  for (JBL_NODE p = r->child; p; p = p->next) {
    if ((p->type == JBV_NONE) || (p->type == JBV_NULL)) {
      p->type = JBV_STR;
      p->vsize = 0;
      p->child = 0;
    }
    if ((p->type != JBV_STR) && (p->type != JBV_I64) && (p->type != JBV_F64)) {
      iwlog_error("invalid codec parameter type: %d", p->type);
      return RCT_ERROR_INVALID_RTP_PARAMETERS;
    }
    if (!strncmp(p->key, "apt", p->klidx)) {
      if (p->type != JBV_I64) {
        REPORT("invalid codec apt parameter");
      }
    }
  }
  rc = jbn_at(n, "/rtcpFeedback", &r);
  if (rc) {
    rc = RCR(jbn_add_item_arr(n, "rtcpFeedback", &r, pool));
  } else if (r->type != JBV_ARRAY) {
    r->type = JBV_ARRAY;
    r->child = 0;
  }
  for (JBL_NODE fb = r->child; fb; fb = fb->next) {
    RCR(rct_validate_rtcp_feedback(fb, pool));
  }
  return 0;
}

static iwrc _rct_validate_rtp_header_extension(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("ext is not an object");
  }

  // kind is optional. If unset set it to an empty string.
  iwrc rc = jbn_at(n, "/kind", &r);
  if (rc || (r->type != JBV_STR)) {
    if (!rc) {
      r->type = JBV_STR;
      r->vsize = 0;
      r->child = 0;
    } else {
      rc = RCR(jbn_add_item_str(n, "kind", 0, 0, &r, pool));
    }
  }

  // uri is mandatory.
  rc = jbn_at(n, "/uri", &r);
  if (rc || (r->type != JBV_STR)) {
    REPORT("missing ext.uri");
  }

  // preferredId is mandatory.
  rc = jbn_at(n, "/preferredId", &r);
  if (rc || (r->type != JBV_I64)) {
    REPORT("missing ext.preferredId");
  }

  // preferredEncrypt is optional. If unset set it to false.
  rc = jbn_at(n, "/preferredEncrypt", &r);
  if (rc) {
    rc = RCR(jbn_add_item_bool(n, "preferredEncrypt", false, &r, pool));
  } else if (r->type != JBV_BOOL) {
    REPORT("invalid ext.preferredEncrypt");
  }

  // direction is optional. If unset set it to sendrecv.
  rc = jbn_at(n, "/direction", &r);
  if (rc) {
    rc = RCR(jbn_add_item_str(n, "direction", "sendrecv", -1, &r, pool));
  } else if (r->type != JBV_STR) {
    REPORT("invalid ext.direction");
  }

  return 0;
}

static iwrc _rct_validate_rtp_capabilities(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("caps is not an object");
  }

  // codecs is optional. If unset, fill with an empty array.
  iwrc rc = jbn_at(n, "/codecs", &r);
  if (rc) {
    rc = RCR(jbn_add_item_arr(n, "codecs", &r, pool));
  } else if (r->type != JBV_ARRAY) {
    REPORT("caps.codecs is not an array");
  }

  for (JBL_NODE codec = r->child; codec; codec = codec->next) {
    RCR(_rct_validate_rtp_codec_capability(codec, pool));
  }

  // headerExtensions is optional. If unset, fill with an empty array.
  rc = jbn_at(n, "/headerExtensions", &r);
  if (rc) {
    rc = RCR(jbn_add_item_arr(n, "headerExtensions", &r, pool));
  } else if (r->type != JBV_ARRAY) {
    REPORT("caps.headerExtensions is not an array");
  }

  for (JBL_NODE ext = r->child; ext; ext = ext->next) {
    RCR(_rct_validate_rtp_header_extension(ext, pool));
  }

  return 0;
}

static bool _rct_find_header_extension_by_uri(JBL_NODE extensions, const char *uri, JBL_NODE *ext_out) {
  iwrc rc = 0;
  *ext_out = 0;
  for (JBL_NODE ext = extensions->child; ext; ext = ext->next) {
    if (!jbn_path_compare_str(ext, "/uri", uri, &rc)) {
      if (!rc) {
        *ext_out = ext;
      }
      break;
    }
  }
  return *ext_out != 0;
}

static iwrc _rct_consumer_produce_input(rct_consumer_t *consumer, wrc_worker_input_t *input) {
  iwrc rc = 0, rc2;

  JBL_NODE n1,

           consumer_rtp_params,
           consumer_header_extensions,
           consumer_encodings,
           consumer_codecs,

           consumable_codecs,
           consumable_header_extensions,
           consumable_encodings,

           caps_codecs,
           caps_header_extensions,

           data,

           consumable_rtp_parameters,
           rtp_capabilities;

  bool bv, rtx_supported = false;

  rct_producer_t *producer = (void*) consumer->producer;
  if (producer->type != RCT_TYPE_PRODUCER) {
    iwlog_error("RCT Consumer must be linked only with producer of type RCT_TYPE_PRODUCER");
    return RCT_ERROR_INVALID_RESOURCE_CONFIGURATION;
  }

  IWPOOL *pool = iwpool_create(1024);
  RCA(pool, finish);

  RCC(rc, finish,
      jbn_clone(producer->spec->consumable_rtp_parameters, &consumable_rtp_parameters, pool));

  RCC(rc, finish,
      jbn_clone(consumer->rtp_capabilities, &rtp_capabilities, pool));

  RCC(rc, finish,
      jbn_from_json("{\"codecs\":[],\"headerExtensions\":[],\"encodings\":[]}",
                    &consumer_rtp_params, pool));

  RCC(rc, finish, jbn_copy_path(
        consumable_rtp_parameters, "/rtcp",
        consumer_rtp_params, "/rtcp", true, false, pool));

  RCC(rc, finish, jbn_at(consumer_rtp_params, "/codecs", &consumer_codecs));
  RCC(rc, finish, jbn_at(consumer_rtp_params, "/headerExtensions", &consumer_header_extensions));
  RCC(rc, finish, jbn_at(consumer_rtp_params, "/encodings", &consumer_encodings));

  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/codecs", &consumable_codecs));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/headerExtensions", &consumable_header_extensions));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/encodings", &consumable_encodings));

  RCC(rc, finish, jbn_at(rtp_capabilities, "/codecs", &caps_codecs));
  RCC(rc, finish, jbn_at(rtp_capabilities, "/headerExtensions", &caps_header_extensions));

  for (JBL_NODE cap_codec = caps_codecs->child; cap_codec; cap_codec = cap_codec->next) {
    RCC(rc, finish, _rct_validate_rtp_codec_capability(cap_codec, pool));
  }

  for (JBL_NODE codec = consumable_codecs->child; codec; codec = codec->next) {
    JBL_NODE matched_cap_codec = 0;
    for (JBL_NODE cap_codec = caps_codecs->child; cap_codec; cap_codec = cap_codec->next) {
      RCC(rc, finish, rct_codecs_is_matched(cap_codec, codec, true, false, pool, &bv));
      if (bv) {
        matched_cap_codec = cap_codec;
        break;
      }
    }
    if (matched_cap_codec) {
      RCC(rc, finish, jbn_clone(codec, &n1, pool));
      RCC(rc, finish, jbn_copy_path(matched_cap_codec, "/rtcpFeedback", n1, "/rtcpFeedback", false, false, pool));
      jbn_add_item(consumer_codecs, n1);
    }
  }

  // Must sanitize the list of matched codecs by removing useless RTX codecs.
  for (JBL_NODE codec = consumer_codecs->child, next; codec; codec = next) {
    next = codec->next;
    if (rct_codec_is_rtx(codec)) {
      JBL_NODE associated_codec = 0;
      for (JBL_NODE cc = consumer_codecs->child; cc; cc = cc->next) {
        iwrc rc2 = 0;
        int rv = jbn_paths_compare(cc, "/payloadType", codec, "/parameters/apt", JBV_I64, &rc2);
        if (!rc2 && !rv) {
          associated_codec = cc;
          break;
        }
      }
      if (associated_codec) {
        rtx_supported = true;
      } else {
        jbn_remove_item(consumer_codecs, codec);
      }
    }
  }

  // Ensure there is at least one media codec.
  if (!consumer_codecs->child || rct_codec_is_rtx(consumer_codecs->child)) {
    iwlog_error2("no compatible media codecs");
    rc = RCT_ERROR_INVALID_RTP_PARAMETERS;
    goto finish;
  }

  for (JBL_NODE ext = consumable_header_extensions->child; ext; ext = ext->next) {
    for (JBL_NODE cap_ext = caps_header_extensions->child; cap_ext; cap_ext = cap_ext->next) {
      int rv = jbn_paths_compare(cap_ext, "/preferredId", ext, "/id", JBV_I64, &rc);
      RCGO(rc, finish);
      if (!rv) {
        rv = jbn_paths_compare(cap_ext, "/uri", ext, "/uri", JBV_STR, &rc);
        RCGO(rc, finish);
        if (!rv) {
          RCC(rc, finish, jbn_clone(ext, &n1, pool));
          jbn_add_item(consumer_header_extensions, n1);
          break;
        }
      }
    }
  }

  // Reduce codecs' RTCP feedback. Use Transport-CC if available, REMB otherwise.
  if (_rct_find_header_extension_by_uri(
        consumer_header_extensions,
        "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01",
        &n1)) {
    for (JBL_NODE codec = consumer_codecs->child; codec; codec = codec->next) {
      RCC(rc, finish, jbn_at(codec, "/rtcpFeedback", &n1));
      for (JBL_NODE fb = n1->child; fb; fb = fb->next) {
        int rv = jbn_path_compare_str(fb, "/type", "goog-remb", &rc);
        RCGO(rc, finish);
        if (!rv) {
          JBL_NODE next = fb->next;
          jbn_remove_item(n1, fb);
          fb->next = next;
        }
      }
    }
  } else if (_rct_find_header_extension_by_uri(
               consumer_header_extensions,
               "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
               &n1)) {
    for (JBL_NODE codec = consumer_codecs->child; codec; codec = codec->next) {
      RCC(rc, finish, jbn_at(codec, "/rtcpFeedback", &n1));
      for (JBL_NODE fb = n1->child; fb; fb = fb->next) {
        int rv = jbn_path_compare_str(fb, "/type", "transport-cc", &rc);
        RCGO(rc, finish);
        if (!rv) {
          JBL_NODE next = fb->next;
          jbn_remove_item(n1, fb);
          fb->next = next;
        }
      }
    }
  } else {
    for (JBL_NODE codec = consumer_codecs->child; codec; codec = codec->next) {
      RCC(rc, finish, jbn_at(codec, "/rtcpFeedback", &n1));
      for (JBL_NODE fb = n1->child; fb; fb = fb->next) {
        int rv = jbn_path_compare_str(fb, "/type", "transport-cc", &rc);
        RCGO(rc, finish);
        if (rv) {
          rv = jbn_path_compare_str(fb, "/type", "goog-remb", &rc);
          RCGO(rc, finish);
        }
        if (!rv) {
          JBL_NODE next = fb->next;
          jbn_remove_item(n1, fb);
          fb->next = next;
        }
      }
    }
  }

  JBL_NODE consumer_encoding;
  const char *scalability_mode = 0;

  RCC(rc, finish, jbn_from_json("{}", &consumer_encoding, pool));
  RCC(rc, finish, jbn_add_item_i64(consumer_encoding, "ssrc", 100000000 + iwu_rand_range(900000000), 0, pool));
  if (rtx_supported) {
    RCC(rc, finish, jbn_add_item_obj(consumer_encoding, "rtx", &n1, pool));
    RCC(rc, finish, jbn_add_item_i64(n1, "ssrc", 100000000 + iwu_rand_range(900000000), 0, pool));
  }

  // If any of the consumableParams.encodings has scalabilityMode, process it
  // (assume all encodings have the same value).
  for (JBL_NODE enc = consumable_encodings->child; enc; enc = enc->next) {
    rc2 = jbn_at(enc, "/scalabilityMode", &n1);
    if (!rc2 && (n1->type == JBV_STR)) {
      scalability_mode = n1->vptr;
      break;
    }
  }

  // If there is simulast, mangle spatial layers in scalabilityMode.
  if (consumable_encodings->child && consumable_encodings->child->next) {
    int temporalLayers;
    rct_parse_scalability_mode(scalability_mode, 0, &temporalLayers, 0);
    RCB(finish, scalability_mode = iwpool_printf(pool, "S%dT%d", jbn_length(consumable_encodings), temporalLayers));
  }

  if (scalability_mode) {
    RCC(rc, finish, jbn_add_item_str(consumer_encoding, "scalabilityMode", scalability_mode, -1, 0, pool));
  }

  // Use the maximum maxBitrate in any encoding and honor it in the Consumer's
  // encoding.
  int max_encoding_max_bitrate = 0;
  for (JBL_NODE encoding = consumable_encodings->child; encoding; encoding = encoding->next) {
    rc2 = jbn_at(encoding, "/maxBitrate", &n1);
    if (!rc2 && (n1->type == JBV_I64)) {
      if (n1->vi64 > max_encoding_max_bitrate) {
        max_encoding_max_bitrate = n1->vi64;
      }
    }
  }
  if (max_encoding_max_bitrate) {
    RCC(rc, finish, jbn_add_item_i64(consumer_encoding, "maxBitrate", max_encoding_max_bitrate, 0, pool));
  }

  // Set a single encoding for the Consumer.
  jbn_add_item(consumer_encodings, consumer_encoding);

  // Copy verbatim.
  jbn_copy_path(consumable_rtp_parameters, "/rtcp", consumer_rtp_params, "/rtcp", false, false, pool);

  // Set MID
  {
    char buf[NUMBUSZ];
    int mid = rct_transport_next_mid_lk(consumer->transport);
    iwitoa(mid, buf, sizeof(buf));
    RCC(rc, finish, jbn_add_item_str(consumer_rtp_params, "mid", buf, -1, 0, pool));
  }

  // No assemble input data
  const char *kind = (producer->spec->rtp_kind & RTP_KIND_VIDEO) ? "video" : "audio";
  RCC(rc, finish, jbn_from_json("{}", &data, pool));
  RCC(rc, finish, jbn_add_item_str(data, "kind", kind, -1, 0, pool));

  consumer_rtp_params->key = "rtpParameters";
  consumer_rtp_params->klidx = (int) strlen(consumer_rtp_params->key);
  jbn_add_item(data, consumer_rtp_params);

  RCC(rc, finish, jbn_add_item_str(data, "type", producer->producer_type_str, -1, 0, pool));

  consumable_encodings->key = "consumableRtpEncodings";
  consumable_encodings->klidx = (int) strlen(consumable_encodings->key);
  jbn_add_item(data, consumable_encodings);

  RCC(rc, finish, jbn_add_item_bool(data, "paused", consumer->paused, 0, pool));

  if (consumer->preferred_layer.spartial != -1) {
    RCC(rc, finish, jbn_add_item_obj(data, "preferredLayers", &n1, pool));
    RCC(rc, finish, jbn_add_item_i64(n1, "spatialLayer", consumer->preferred_layer.spartial, 0, pool));
    if (consumer->preferred_layer.temporal != -1) {
      RCC(rc, finish, jbn_add_item_i64(n1, "temporalLayer", consumer->preferred_layer.temporal, 0, pool));
    }
  }
  RCC(rc, finish, jbl_from_node(&input->data, data));

  // Save consumer params
  RCC(rc, finish, jbn_clone(consumer_rtp_params, &consumer->rtp_parameters, consumer->pool));

finish:
  iwpool_destroy(pool);
  return rc;
}

static iwrc _rct_producer_can_consume(
  wrc_resource_t producer_id,
  const char    *rtp_capabilities_json,
  JBL_NODE       rtp_capabilities_node,
  bool          *out
  ) {
  *out = false;

  if (!rtp_capabilities_json && !rtp_capabilities_node) {
    return 0;
  }

  iwrc rc = 0;
  bool locked = false;
  rct_producer_t *producer = 0;
  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  JBL_NODE consumable_rtp_parameters,
           codecs,
           cap_codecs,
           rtp_capabilities;

  if (rtp_capabilities_json) {
    RCC(rc, finish, jbn_from_json(rtp_capabilities_json, &rtp_capabilities, pool));
  } else {
    RCC(rc, finish, jbn_clone(rtp_capabilities_node, &rtp_capabilities, pool));
  }

  producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER, __func__);
  locked = true;

  RCIF(!producer, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);
  RCC(rc, finish, jbn_clone(producer->spec->consumable_rtp_parameters, &consumable_rtp_parameters, pool));
  rct_resource_unlock_keep_ref((void*) producer), locked = false;

  RCC(rc, finish, _rct_validate_rtp_capabilities(rtp_capabilities, pool));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/codecs", &codecs));
  RCC(rc, finish, jbn_at(rtp_capabilities, "/codecs", &cap_codecs));

  for (JBL_NODE codec = codecs->child; codec; codec = codec->next) {
    JBL_NODE mc = rct_codec_find_matched(codec, cap_codecs, true, false, pool);
    if (mc && !rct_codec_is_rtx(mc)) {
      *out = true;
      goto finish;
    }
  }

finish:
  rct_resource_ref_unlock(producer, locked, -1, __func__);
  iwpool_destroy(pool);
  return rc;
}

iwrc rct_producer_can_consume(wrc_resource_t producer_id, const char *rtp_capabilities, bool *out) {
  return _rct_producer_can_consume(producer_id, rtp_capabilities, 0, out);
}

iwrc rct_producer_can_consume2(wrc_resource_t producer_id, JBL_NODE rtp_capabilities, bool *out) {
  return _rct_producer_can_consume(producer_id, 0, rtp_capabilities, out);
}

static iwrc _rct_consumer_create(
  wrc_resource_t        transport_id,
  wrc_resource_t        producer_id,
  const char           *rtp_capabilities_json,
  JBL_NODE              rtp_capabilities_node,
  bool                  paused,
  rct_consumer_layer_t *preferred_layer,
  wrc_resource_t       *consumer_out
  ) {
  *consumer_out = 0;

  if (!rtp_capabilities_json && !rtp_capabilities_node) {
    return IW_ERROR_INVALID_ARGS;
  }

  iwrc rc = 0, rc2;
  JBL_NODE n1;
  wrc_msg_t *m = 0;
  bool locked = false;
  rct_consumer_t *consumer;
  rct_transport_t *transport = 0;
  rct_producer_t *producer = 0;


  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  RCB(finish, consumer = iwpool_calloc(sizeof(*consumer), pool));
  consumer->pool = pool;
  consumer->type = RCT_TYPE_CONSUMER;
  consumer->priority = 1;
  consumer->paused = paused;
  consumer->current_layer = (rct_consumer_layer_t) {
    -1, -1
  };
  if (preferred_layer) {
    consumer->preferred_layer = *preferred_layer;
  } else {
    consumer->preferred_layer = (rct_consumer_layer_t) {
      -1, -1
    };
  }
  iwu_uuid4_fill(consumer->uuid);

  producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER, __func__);
  locked = true;
  RCIF(!producer, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  transport = rct_resource_ref_lk(rct_resource_by_id_unsafe(transport_id, RCT_TYPE_TRANSPORT_ALL), 1, __func__);
  RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  rct_resource_ref_lk(consumer, RCT_INIT_REFS, __func__);
  consumer->producer = rct_resource_ref_lk(producer, 1, __func__);
  consumer->transport = rct_resource_ref_lk(transport, 1, __func__);

  if (rtp_capabilities_json) {
    RCC(rc, finish, jbn_from_json(rtp_capabilities_json, &consumer->rtp_capabilities, pool));
  } else {
    RCC(rc, finish, jbn_clone(rtp_capabilities_node, &consumer->rtp_capabilities, pool));
  }

  RCC(rc, finish, _rct_validate_rtp_capabilities(consumer->rtp_capabilities, pool));

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = transport->router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_TRANSPORT_CONSUME
      }
    }
  }));
  RCC(rc, finish, jbl_clone(transport->identity, &m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "producerId", producer->uuid));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "consumerId", consumer->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &consumer->identity, pool));
  RCC(rc, finish, _rct_consumer_produce_input(consumer, &m->input.worker));

  // Register in producer
  rct_consumer_base_t *pp = producer->consumers;
  producer->consumers = (void*) consumer;
  consumer->next = pp;

  // Register in own transport
  pp = transport->consumers;
  transport->consumers = (void*) consumer;
  consumer->next_transport = pp;

  consumer->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(consumer));
  rct_resource_unlock_keep_ref((void*) producer), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  if (m->output.worker.data) {
    JBL_NODE out;
    RCC(rc, finish, jbl_to_node(m->output.worker.data, &out, false, pool));
    rc2 = jbn_at(out, "/data/paused", &n1);
    if (!rc2 && (n1->type == JBV_BOOL)) {
      consumer->paused = n1->vbool;
    }
    // We will not take into account /data/producerPaused data
    //rc2 = jbn_at(out, "/data/producerPaused", &n1);
    //if (!rc2 && (n1->type == JBV_BOOL)) {
    // consumer->producer_paused = n1->vbool;
    //}
    rc2 = jbn_at(out, "/data/score/score", &n1);
    if (!rc2 && (n1->type == JBV_I64)) {
      consumer->score = n1->vi64;
    }
    rc2 = jbn_at(out, "/data/score/producerScore", &n1);
    if (!rc2 && (n1->type == JBV_I64)) {
      consumer->producer_score = n1->vi64;
    }
    rc2 = jbn_at(out, "/data/score/producerScores", &n1);
    if (!rc2 && (n1->type == JBV_ARRAY)) {
      RCC(rc, finish, jbl_from_node(&consumer->producer_scores, n1));
    }
    rc2 = jbn_at(out, "/data/preferredLayers/spatialLayer", &n1);
    if (!rc2 && (n1->type == JBV_I64)) {
      consumer->preferred_layer.spartial = n1->vi64;
    }
    rc2 = jbn_at(out, "/data/preferredLayers/temporalLayer", &n1);
    if (!rc2 && (n1->type == JBV_I64)) {
      consumer->preferred_layer.temporal = n1->vi64;
    }
  } else {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  wrc_notify_event_handlers(WRC_EVT_CONSUMER_CREATED, consumer->id, 0);
  *consumer_out = consumer->id;

finish:
  rct_resource_ref_keep_locking(consumer, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_keep_locking(transport, locked, -1, __func__);
  rct_resource_ref_unlock(producer, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}

iwrc rct_consumer_create(
  wrc_resource_t        transport_id,
  wrc_resource_t        producer_id,
  const char           *rtp_capabilities_json,
  bool                  paused,
  rct_consumer_layer_t *preferred_layer,
  wrc_resource_t       *consumer_out
  ) {
  return _rct_consumer_create(transport_id, producer_id, rtp_capabilities_json, 0,
                              paused, preferred_layer, consumer_out);
}

iwrc rct_consumer_create2(
  wrc_resource_t        transport_id,
  wrc_resource_t        producer_id,
  JBL_NODE              rtp_capabilities,
  bool                  paused,
  rct_consumer_layer_t *preferred_layer,
  wrc_resource_t       *consumer_out
  ) {
  return _rct_consumer_create(transport_id, producer_id, 0, rtp_capabilities,
                              paused, preferred_layer, consumer_out);
}

iwrc rct_consumer_set_preferred_layers(
  wrc_resource_t       consumer_id,
  rct_consumer_layer_t layer
  ) {
  JBL jbl, cmd_out = 0;
  iwrc rc = RCR(jbl_create_empty_object(&jbl));
  if (layer.spartial != -1) {
    RCC(rc, finish, jbl_set_int64(jbl, "spatialLayer", layer.spartial));
    if (layer.temporal != -1) {
      RCC(rc, finish, jbl_set_int64(jbl, "temporalLayer", layer.temporal));
    }
  }

  RCC(rc, finish, rct_resource_json_command(consumer_id,
                                            WRC_CMD_CONSUMER_SET_PREFERRED_LAYERS, RCT_TYPE_CONSUMER, jbl, &cmd_out));

  rct_consumer_t *c = rct_resource_by_id_locked(consumer_id, RCT_TYPE_CONSUMER, __func__);
  if (c) {
    iwrc rc2;
    int64_t spartial = -2, temporal = -2;

    jbl_destroy(&jbl);
    rc2 = jbl_at(cmd_out, "/data/spatialLayer", &jbl);
    if (!rc2 && (jbl_type(jbl) == JBV_I64)) {
      spartial = jbl_get_i64(jbl);
    }
    jbl_destroy(&jbl);
    rc2 = jbl_at(cmd_out, "/data/temporalLayer", &jbl);
    if (!rc2 && (jbl_type(jbl) == JBV_I64)) {
      temporal = jbl_get_i64(jbl);
    }
    if (spartial != -2) {
      c->preferred_layer.spartial = spartial;
    }
    if (temporal != -2) {
      c->preferred_layer.temporal = temporal;
    }
  }
  rct_resource_unlock(c, __func__);

finish:
  jbl_destroy(&cmd_out);
  jbl_destroy(&jbl);
  return rc;
}

iwrc rct_consumer_set_priority(wrc_resource_t consumer_id, int priority) {
  JBL jbl, cmd_out = 0;
  rct_consumer_t *c = 0;
  iwrc rc = RCR(jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_int64(jbl, "priority", priority));
  RCC(rc, finish, rct_resource_json_command(consumer_id,
                                            WRC_CMD_CONSUMER_SET_PRIORITY, RCT_TYPE_CONSUMER, jbl, &cmd_out));
  c = rct_resource_by_id_locked_exists(consumer_id, RCT_TYPE_CONSUMER, __func__);
  if (c) {
    jbl_destroy(&jbl);
    rc = jbl_at(cmd_out, "/data/priority", &jbl);
    if (!rc && (jbl_type(jbl) == JBV_I64)) {
      c->priority = jbl_get_i64(jbl);
    }
  }

finish:
  rct_resource_unlock_exists(c, __func__);
  jbl_destroy(&cmd_out);
  jbl_destroy(&jbl);
  return rc;
}

iwrc rct_consumer_unset_priority(wrc_resource_t consumer_id) {
  return rct_consumer_set_priority(consumer_id, 1);
}

iwrc rct_consumer_request_key_frame(wrc_resource_t consumer_id) {
  return rct_resource_json_command_async(consumer_id, WRC_CMD_CONSUMER_REQUEST_KEY_FRAME, RCT_TYPE_CONSUMER, 0);
}

iwrc rct_consumer_stats(wrc_resource_t consumer_id, JBL *dump_out) {
  rct_resource_base_t b;
  RCR(rct_resource_probe_by_id(consumer_id, &b));
  if (b.type == RCT_TYPE_CONSUMER) {
    return rct_resource_json_command(consumer_id, WRC_CMD_CONSUMER_GET_STATS, b.type, 0, dump_out);
  } else if (b.type == RCT_TYPE_CONSUMER_DATA) {
    return rct_resource_json_command(consumer_id, WRC_CMD_DATA_CONSUMER_GET_STATS, b.type, 0, dump_out);
  } else {
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
}

iwrc rct_consumer_dump(wrc_resource_t consumer_id, JBL *dump_out) {
  rct_resource_base_t b;
  RCR(rct_resource_probe_by_id(consumer_id, &b));
  if (b.type == RCT_TYPE_CONSUMER) {
    return rct_resource_json_command(consumer_id, WRC_CMD_CONSUMER_DUMP, b.type, 0, dump_out);
  } else if (b.type == RCT_TYPE_CONSUMER_DATA) {
    return rct_resource_json_command(consumer_id, WRC_CMD_DATA_CONSUMER_DUMP, b.type, 0, dump_out);
  } else {
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
}

iwrc rct_consumer_is_paused(wrc_resource_t consumer_id, bool *paused_out) {
  rct_consumer_t *c = rct_resource_by_id_locked_exists(consumer_id, RCT_TYPE_CONSUMER_ALL, __func__);
  if (!c) {
    *paused_out = false;
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
  *paused_out = c->paused || (c->producer && c->producer->paused);
  rct_resource_unlock(c, __func__);
  return 0;
}

iwrc rct_consumer_pause(wrc_resource_t consumer_id) {
  rct_consumer_t *c = rct_resource_by_id_locked_exists(consumer_id, RCT_TYPE_CONSUMER_ALL, __func__);
  if (!c) {
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
  c->paused = true;
  rct_resource_unlock(c, __func__);
  iwrc rc = rct_resource_json_command_async(consumer_id, WRC_CMD_CONSUMER_PAUSE, RCT_TYPE_CONSUMER_ALL, 0);
  if (!rc) {
    wrc_notify_event_handlers(WRC_EVT_CONSUMER_PAUSE, consumer_id, 0);
  }
  return rc;
}

iwrc rct_consumer_resume(wrc_resource_t consumer_id) {
  rct_consumer_t *c = rct_resource_by_id_locked_exists(consumer_id, RCT_TYPE_CONSUMER_ALL, __func__);
  if (!c) {
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
  bool producer_paused = c->producer == 0 || c->producer->paused;
  c->paused = false;
  rct_resource_unlock(c, __func__);
  iwrc rc = rct_resource_json_command_async(consumer_id, WRC_CMD_CONSUMER_RESUME, RCT_TYPE_CONSUMER_ALL, 0);
  if (!rc && !producer_paused) {
    wrc_notify_event_handlers(WRC_EVT_CONSUMER_RESUME, consumer_id, 0);
  }
  return rc;
}

iwrc rct_consumer_enable_trace_events(wrc_resource_t consumer_id, uint32_t events) {
  JBL jbl = 0, jbl2 = 0;
  iwrc rc = jbl_create_empty_object(&jbl);
  RCGO(rc, finish);
  RCC(rc, finish, jbl_create_empty_array(&jbl2));

  if (events & CONSUMER_TRACE_EVENT_RTP) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "rtp"));
  }
  if (events & CONSUMER_TRACE_EVENT_KEYFRAME) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "keyframe"));
  }
  if (events & CONSUMER_TRACE_EVENT_NACK) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "nack"));
  }
  if (events & CONSUMER_TRACE_EVENT_PLI) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "pli"));
  }
  if (events & CONSUMER_TRACE_EVENT_FIR) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "fir"));
  }

  RCC(rc, finish, jbl_set_nested(jbl, "types", jbl2));
  rc = rct_resource_json_command(consumer_id, WRC_CMD_CONSUMER_ENABLE_TRACE_EVENT, 0, jbl, 0);

finish:
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  return rc;
}

static uint32_t _event_handler_id;

static iwrc _rct_event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  switch (evt) {
    case WRC_EVT_CONSUMER_LAYERSCHANGE: {
      if (!data) {
        break;
      }
      JBL n = 0;
      int sl = -1, tl = -1;
      iwrc rc = jbl_at(data, "/data/spatialLayer", &n);
      if (!rc && (jbl_type(n) == JBV_I64)) {
        sl = jbl_get_i32(n);
      }
      jbl_destroy(&n);
      rc = jbl_at(data, "/data/temporalLayer", &n);
      if (!rc && (jbl_type(n) == JBV_I64)) {
        tl = jbl_get_i32(n);
      }
      jbl_destroy(&n);

      rct_consumer_t *c = rct_resource_by_id_locked(
        resource_id, RCT_TYPE_CONSUMER,
        "consumer_event_handler:WRC_EVT_CONSUMER_LAYERSCHANGE");
      if (c) {
        c->current_layer.spartial = sl;
        c->current_layer.temporal = tl;
      }
      rct_resource_unlock(c, "consumer_event_handler:WRC_EVT_CONSUMER_LAYERSCHANGE");
      break;
    }

    case WRC_EVT_RESOURCE_SCORE: {
      if (!data) {
        break;
      }
      rct_consumer_t *c = rct_resource_by_id_locked(
        resource_id, RCT_TYPE_CONSUMER,
        "consumer_event_handler:WRC_EVT_RESOURCE_SCORE");
      if (c) {
        JBL ps, cs, pss;
        jbl_at(data, "/data/score", &cs);
        jbl_at(data, "/data/producerScore", &ps);
        jbl_at(data, "/data/producerScores", &pss);

        if (jbl_type(ps) == JBV_I64) {
          c->producer_score = jbl_get_i32(ps);
        }
        if (jbl_type(cs) == JBV_I64) {
          c->score = jbl_get_i32(cs);
        }
        if (jbl_type(pss) == JBV_ARRAY) {
          jbl_destroy(&c->producer_scores); // Dispose old scores
          c->producer_scores = pss;
          pss = 0; // Transfer ownership
        }
        jbl_destroy(&ps);
        jbl_destroy(&cs);
        jbl_destroy(&pss);
      }
      rct_resource_unlock(c, "consumer_event_handler:WRC_EVT_RESOURCE_SCORE");
      break;
    }

    case WRC_EVT_CONSUMER_PRODUCER_PAUSE: {
      rct_consumer_t *c = rct_resource_by_id_locked(
        resource_id, RCT_TYPE_CONSUMER_ALL,
        "consumer_event_handler:WRC_EVT_CONSUMER_PRODUCER_PAUSE");
      bool cp = c && c->paused;
      rct_resource_unlock(c, "consumer_event_handler:WRC_EVT_CONSUMER_PRODUCER_PAUSE");
      if (c && !cp) {
        wrc_notify_event_handlers(WRC_EVT_CONSUMER_PAUSE, resource_id, 0);
      }
      break;
    }

    case WRC_EVT_CONSUMER_PRODUCER_RESUME: {
      rct_consumer_t *c = rct_resource_by_id_locked(
        resource_id, RCT_TYPE_CONSUMER_ALL,
        "consumer_event_handler:WRC_EVT_CONSUMER_PRODUCER_RESUME");
      bool cp = c && c->paused;
      rct_resource_unlock(c, "consumer_event_handler:WRC_EVT_CONSUMER_PRODUCER_RESUME");
      if (c) {
        if (!cp) {
          wrc_notify_event_handlers(WRC_EVT_CONSUMER_RESUME, resource_id, 0);
        } else if (c->resume_by_producer) {
          // Wake up when producer resumed
          rct_consumer_resume(c->id);
        }
      }
      break;
    }

    default:
      break;
  }
  return 0;
}

iwrc rct_consumer_module_init(void) {
  return wrc_add_event_handler(_rct_event_handler, 0, &_event_handler_id);
}

void rct_consumer_module_close(void) {
  wrc_remove_event_handler(_event_handler_id);
}
