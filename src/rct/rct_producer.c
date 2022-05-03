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

#include "rct_producer.h"
#include "rct_consumer.h"
#include "rct_h264.h"
#include "utils/utf8.h"
#include "utils/network.h"

#include <iowow/iwutils.h>
#include <iowow/iwarr.h>
#include <iowow/iwstree.h>
#include <assert.h>

#define REPORT(msg_)                         \
  iwlog_error2(msg_);                        \
  return RCT_ERROR_INVALID_RTP_PARAMETERS;

void rct_producer_dispose_lk(rct_producer_base_t *producer) {
  if (producer->transport) {
    for (rct_producer_base_t *p = producer->transport->producers, *pp = 0; p; p = p->next) {
      if (p->id == producer->id) {
        if (pp) {
          pp->next = p->next;
        } else {
          producer->transport->producers = p->next;
        }
        break;
      } else {
        pp = p;
      }
    }
  }
  // Unref parent transport
  rct_resource_ref_lk(producer->transport, -1, __func__);
}

void rct_producer_close_lk(rct_producer_base_t *producer) {
  for (rct_consumer_base_t *p = producer->consumers, *n; p; p = n) {
    n = p->next;
    rct_resource_close_lk(p);
  }
  if (producer->type == RCT_TYPE_PRODUCER) {
    rct_producer_t *p = (void*) producer;
    if (p->export) { // Close depended export
      rct_resource_close_lk(p->export);
    }
  }
}

static iwrc _rct_validate_rtp_codec_parameters(JBL_NODE n, IWPOOL *pool) {
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
  rc = jbn_at(n, "/payloadType", &r);
  if (rc || (r->type != JBV_I64)) {
    REPORT("missing codec.payloadType");
  }
  rc = jbn_at(n, "/clockRate", &r);
  if (rc || (r->type != JBV_I64)) {
    REPORT("missing codec.clockRate");
  }
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
  rc = jbn_at(n, "/parameters", &r);
  if (rc) {
    rc = RCR(jbn_add_item_obj(n, "parameters", 0, pool));
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

static iwrc _rct_validate_rtp_header_extensions_parameters(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("ext is not an object");
  }
  iwrc rc = jbn_at(n, "/uri", &r);
  if (rc || (r->type != JBV_STR)) {
    REPORT("missing ext.uri");
  }
  rc = jbn_at(n, "/id", &r);
  if (rc || (r->type != JBV_I64)) {
    REPORT("missing ext.id");
  }
  rc = jbn_at(n, "/encrypt", &r);
  if (rc) {
    rc = RCR(jbn_add_item_bool(n, "encrypt", false, &r, pool));
  } else if (r->type != JBV_BOOL) {
    r->type = JBV_BOOL;
    r->vbool = false;
    r->child = 0;
  }
  rc = jbn_at(n, "/parameters", &r);
  if (rc) {
    rc = RCR(jbn_add_item_obj(n, "parameters", &r, pool));
  } else if (r->type != JBV_OBJECT) {
    r->type = JBV_OBJECT;
    r->child = 0;
  }
  for (JBL_NODE cn = r->child; cn; cn = cn->next) {
    if ((cn->type == JBV_NULL) || (cn->type == JBV_NONE)) {
      cn->type = JBV_STR;
      cn->vptr = 0;
      cn->vsize = 0;
    }
    if ((cn->type != JBV_STR) && (cn->type != JBV_I64) && (cn->type != JBV_F64)) {
      REPORT("invalid header extension parameter");
    }
  }
  return 0;
}

static iwrc _rct_validate_rtcp_parameters(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("rtcp is not an object");
  }
  iwrc rc = jbn_at(n, "/cname", &r);
  if (!rc && (r->type != JBV_STR)) {
    REPORT("invalid rtcp.cname");
  }
  rc = jbn_at(n, "/reducedSize", &r);
  if (rc) {
    rc = RCR(jbn_add_item_bool(n, "reducedSize", true, &r, pool));
  } else if (r->type != JBV_BOOL) {
    r->type = JBV_BOOL;
    r->vbool = false;
    r->child = 0;
  }
  return 0;
}

static iwrc _rct_validate_rtp_encoding_parameters(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("encoding is not an object");
  }
  iwrc rc = jbn_at(n, "/ssrc", &r);
  if (!rc && (r->type != JBV_I64)) {
    REPORT("invalid encoding.ssrc");
  }
  rc = jbn_at(n, "/rid", &r);
  if (!rc && (r->type != JBV_STR)) {
    REPORT("invalid encoding.rid");
  }
  rc = jbn_at(n, "/rtx", &r);
  if (!rc) {
    if (r->type != JBV_OBJECT) {
      REPORT("invalid encoding.rtx");
    }
    rc = jbn_at(n, "/rtx/ssrc", &r);
    if (!rc && (r->type != JBV_I64)) {
      REPORT("invalid encoding.rtx.ssrc");
    }
  }
  rc = jbn_at(n, "/dtx", &r);
  if (rc) {
    rc = RCR(jbn_add_item_bool(n, "dtx", false, &r, pool));
  } else if (r->type != JBV_BOOL) {
    r->type = JBV_BOOL;
    r->vbool = false;
    r->child = 0;
  }
  rc = jbn_at(n, "/scalabilityMode", &r);
  if (!rc && (r->type != JBV_STR)) {
    REPORT("invalid encoding.scalabilityMode");
  }
  return 0;
}

static iwrc _rct_validate_rtp_parameters(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE r;
  if (n->type != JBV_OBJECT) {
    REPORT("params is not an object");
  }

  iwrc rc = jbn_at(n, "/mid", &r);
  if (!rc && (r->type != JBV_STR)) {
    REPORT("params.mid is not a string");
  }

  rc = jbn_at(n, "/codecs", &r);
  if (rc || (r->type != JBV_ARRAY)) {
    REPORT("missing params.codecs");
  }
  for (JBL_NODE cn = r->child; cn; cn = cn->next) {
    RCR(_rct_validate_rtp_codec_parameters(cn, pool));
  }

  rc = jbn_at(n, "/headerExtensions", &r);
  if (rc) {
    rc = RCR(jbn_add_item_arr(n, "headerExtensions", &r, pool));
  } else if (r->type != JBV_ARRAY) {
    REPORT("params.headerExtensions is not an array");
  }
  for (JBL_NODE cn = r->child; cn; cn = cn->next) {
    RCR(_rct_validate_rtp_header_extensions_parameters(cn, pool));
  }

  rc = jbn_at(n, "/rtcp", &r);
  if (rc) {
    rc = RCR(jbn_add_item_obj(n, "rtcp", &r, pool));
  } else if (r->type != JBV_OBJECT) {
    REPORT("params.rtcp is not an object");
  }
  RCR(_rct_validate_rtcp_parameters(n, pool));

  rc = jbn_at(n, "/encodings", &r);
  if (rc) {
    rc = jbn_add_item_arr(n, "encodings", &r, pool);
  } else if (r->type != JBV_ARRAY) {
    REPORT("params.encodings is not an array");
  }
  for (JBL_NODE cn = r->child; cn; cn = cn->next) {
    RCR(_rct_validate_rtp_encoding_parameters(cn, pool));
  }
  if (!r->child) {
    rc = jbn_add_item_obj(r, 0, 0, pool);
  }
  return rc;
}

static iwrc _rct_producer_spec_create(
  uint32_t rtp_kind,
  const char *rtp_params_json, JBL_NODE rtp_params,
  rct_producer_spec_t **spec_output
  ) {
  if (spec_output) {
    *spec_output = 0;
  } else {
    return IW_ERROR_INVALID_ARGS;
  }
  if (((rtp_kind != RTP_KIND_VIDEO) && (rtp_kind != RTP_KIND_AUDIO)) || ((rtp_params_json == 0) && (rtp_params == 0))) {
    return IW_ERROR_INVALID_ARGS;
  }
  iwrc rc = 0;
  JBL_NODE n;
  IWPOOL *pool = iwpool_create(1024);
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  rct_producer_spec_t *spec = iwpool_calloc(sizeof(*spec), pool);
  RCA(spec, finish);

  spec->pool = pool;
  spec->rtp_kind = rtp_kind;

  RCB(finish, spec->uuid = iwpool_calloc(IW_UUID_STR_LEN + 1, pool));
  iwu_uuid4_fill((void*) spec->uuid);

  if (rtp_params_json) {
    RCC(rc, finish, jbn_from_json(rtp_params_json, &n, pool));
  } else {
    RCC(rc, finish, jbn_clone(rtp_params, &n, pool));
  }
  RCC(rc, finish, _rct_validate_rtp_parameters(n, pool));
  spec->rtp_parameters = n;

finish:
  if (rc) {
    iwpool_destroy(pool);
  } else {
    *spec_output = spec;
  }
  return rc;
}

iwrc rct_producer_spec_create(uint32_t rtp_kind, const char *rtp_params_json, rct_producer_spec_t **spec_output) {
  return _rct_producer_spec_create(rtp_kind, rtp_params_json, 0, spec_output);
}

iwrc rct_producer_spec_create2(uint32_t rtp_kind, JBL_NODE rtp_params, rct_producer_spec_t **spec_output) {
  return _rct_producer_spec_create(rtp_kind, 0, rtp_params, spec_output);
}

iwrc rct_producer_close(wrc_resource_t producer_id) {
  iwrc rc = rct_resource_json_command(producer_id, WRC_CMD_PRODUCER_CLOSE, 0, 0, 0);
  wrc_notify_event_handlers(WRC_EVT_PRODUCER_CLOSED, producer_id, 0);
  return rc;
}

JBL_NODE rct_codec_find_matched(JBL_NODE codec, JBL_NODE codecs, bool strict, bool modify, IWPOOL *pool) {
  bool matched = false;
  for (JBL_NODE cc = codecs->child; cc; cc = cc->next) {
    iwrc rc = rct_codecs_is_matched(codec, cc, strict, modify, pool, &matched);
    if (rc) {
      iwlog_ecode_warn3(rc);
    }
    if (matched) {
      return cc;
    }
  }
  return 0;
}

struct _rct_transport_produce_input_mapper_ctx {
  JBL_NODE mapping_codecs;
  IWPOOL  *pool;
};

static bool _rct_transport_produce_input_mapper(void *key, void *val, void *op, iwrc *rcp) {
  iwrc rc = 0;
  struct _rct_transport_produce_input_mapper_ctx *ctx = op;
  assert(ctx);
  IWPOOL *pool = ctx->pool;
  JBL_NODE n1, n2;
  JBL_NODE mapping_codecs = ctx->mapping_codecs, codec = key, cap_codec = val;
  int64_t payload_type, preferred_payload_type;

  RCC(rc, finish, jbn_at(codec, "/payloadType", &n1));
  RCC(rc, finish, jbn_at(cap_codec, "/preferredPayloadType", &n2));
  if ((n1->type != JBV_I64) || (n2->type != JBV_I64)) {
    rc = RCT_ERROR_INVALID_RTP_PARAMETERS;
    goto finish;
  }
  payload_type = n1->vi64;
  preferred_payload_type = n2->vi64;

  RCC(rc, finish, jbn_add_item_obj(mapping_codecs, 0, &n1, pool));
  RCC(rc, finish, jbn_add_item_i64(n1, "payloadType", payload_type, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n1, "mappedPayloadType", preferred_payload_type, 0, pool));

finish:
  *rcp = rc;
  return true;
}

static iwrc _rct_transport_produce_input(rct_producer_t *producer, wrc_worker_input_t *input) {
  iwrc rc = 0, rc2;

  IWSTREE *map_codec2cap = 0;
  rct_producer_spec_t *spec = producer->spec;
  const char *kind = (spec->rtp_kind & RTP_KIND_VIDEO) ? "video" : "audio";

  JBL_NODE rtp_rt_capabilities = producer->transport->router->rtp_capabilities,
           rtp_parameters = producer->spec->rtp_parameters;

  JBL_NODE n1, n2,
           matched_codec,

           rtp_mapping,

           mapping_codecs,
           mapping_encodings,

           params_codecs,
           params_encodings,

           caps_codecs,
           caps_header_extensions,

           consumable_rtp_parameters,
           consumable_codecs,
           consumable_header_extensions,
           consumable_encodings,
           consumable_rtcp,

           data;

  IWPOOL *pool = iwpool_create(1024);
  RCA(pool, finish);

  RCB(finish, map_codec2cap = iwstree_create(0, 0));

  RCC(rc, finish, jbn_from_json("{\"codecs\":[],\"encodings\":[]}", &rtp_mapping, pool));
  RCC(rc, finish, jbn_from_json("{\"codecs\":[],\"headerExtensions\":[],\"encodings\":[],\"rtcp\":{}}",
                                &consumable_rtp_parameters, pool));

  RCC(rc, finish, jbn_at(rtp_rt_capabilities, "/codecs", &caps_codecs));
  RCC(rc, finish, jbn_at(rtp_rt_capabilities, "/headerExtensions", &caps_header_extensions));

  RCC(rc, finish, jbn_at(rtp_parameters, "/codecs", &params_codecs));
  RCC(rc, finish, jbn_at(rtp_parameters, "/encodings", &params_encodings));

  RCC(rc, finish, jbn_at(rtp_mapping, "/codecs", &mapping_codecs));
  RCC(rc, finish, jbn_at(rtp_mapping, "/encodings", &mapping_encodings));

  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/codecs", &consumable_codecs));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/headerExtensions", &consumable_header_extensions));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/encodings", &consumable_encodings));
  RCC(rc, finish, jbn_at(consumable_rtp_parameters, "/rtcp", &consumable_rtcp));

  for (JBL_NODE pc = params_codecs->child; pc; pc = pc->next) {
    if (rct_codec_is_rtx(pc)) {
      continue;
    }
    matched_codec = rct_codec_find_matched(pc, caps_codecs, true, true, pool);
    if (!matched_codec) {
      rc = RCT_ERROR_UNSUPPORTED_CODEC;
      iwlog_ecode_error3(rc);
      goto finish;
    }
    RCC(rc, finish, iwstree_put(map_codec2cap, pc, matched_codec));
  }

  // Match parameters RTX codecs to capabilities RTX codecs
  for (JBL_NODE pc = params_codecs->child; pc; pc = pc->next) {
    if (!rct_codec_is_rtx(pc)) {
      continue;
    }
    JBL_NODE associated_media_codec = 0;
    for (JBL_NODE ppc = params_codecs->child; ppc; ppc = ppc->next) {
      if (!jbn_paths_compare(ppc, "/payloadType", pc, "/parameters/apt", 0, &rc2)) {
        associated_media_codec = ppc;
        break;
      }
    }
    if (!associated_media_codec) {
      rc = RCT_ERROR_NO_RTX_ASSOCIATED_CODEC;
      iwlog_ecode_error3(rc);
      goto finish;
    }
    JBL_NODE cap_media_codex_rtx = 0;
    JBL_NODE cap_media_codec = iwstree_get(map_codec2cap, associated_media_codec);
    if (!cap_media_codec) {
      rc = IW_ERROR_ASSERTION;
      iwlog_ecode_error3(rc);
      goto finish;
    }
    // Ensure that the capabilities media codec has a RTX codec.
    for (JBL_NODE cc = caps_codecs->child; cc; cc = cc->next) {
      if (  rct_codec_is_rtx(cc)
         && !jbn_paths_compare(cc, "/parameters/apt", cap_media_codec, "/preferredPayloadType", 0, &rc2)) {
        cap_media_codex_rtx = cc;
        break;
      }
    }
    if (!cap_media_codex_rtx) {
      rc = RCT_ERROR_NO_RTX_ASSOCIATED_CODEC;
      iwlog_ecode_error3(rc);
      goto finish;
    }
    RCC(rc, finish, iwstree_put(map_codec2cap, pc, cap_media_codex_rtx));
  }

  // Generate codecs mapping
  RCC(rc, finish, iwstree_visit(map_codec2cap, _rct_transport_produce_input_mapper,
                     &(struct _rct_transport_produce_input_mapper_ctx) {
    .mapping_codecs = mapping_codecs,
    .pool = pool
  }));

  // Generate encodings mapping
  uint64_t mapped_ssrc = 100000000 + iwu_rand_range(900000000);
  for (JBL_NODE encoding = params_encodings->child; encoding; encoding = encoding->next) {
    JBL_NODE me;
    RCC(rc, finish, jbn_add_item_obj(mapping_encodings, 0, &me, pool));
    RCC(rc, finish, jbn_add_item_i64(me, "mappedSsrc", mapped_ssrc++, 0, pool));
    rc2 = jbn_at(encoding, "/rid", &n1);
    if (!rc2 && (n1->type == JBV_STR)) {
      RCC(rc, finish, jbn_add_item_str(me, "rid", n1->vptr, n1->vsize, &n1, pool));
    }
    rc2 = jbn_at(encoding, "/ssrc", &n1);
    if (!rc2 && (n1->type == JBV_I64)) {
      RCC(rc, finish, jbn_add_item_i64(me, "ssrc", n1->vi64, &n1, pool));
    }
    rc2 = jbn_at(encoding, "/scalabilityMode", &n1);
    if (!rc2 && (n1->type == JBV_STR)) {
      RCC(rc, finish, jbn_add_item_str(me, "scalabilityMode", n1->vptr, n1->vsize, &n1, pool));
    }
  }

  // Compute consumable rtp parameters
  for (JBL_NODE pc = params_codecs->child; pc; pc = pc->next) {
    if (rct_codec_is_rtx(pc)) {
      continue;
    }
    JBL_NODE consumable_codec = 0,
             matched_cap_codec = 0,
             consumable_cap_rtx_codec = 0,
             consumable_rtx_codec = 0;

    for (JBL_NODE mc = mapping_codecs->child; mc; mc = mc->next) {
      int rv = jbn_paths_compare(pc, "/payloadType", mc, "/payloadType", JBV_I64, &rc);
      RCGO(rc, finish);
      if (!rv) {
        consumable_codec = mc;
        break;
      }
    }
    if (!consumable_codec) {
      rc = IW_ERROR_ASSERTION;
      iwlog_ecode_error3(rc);
      goto finish;
    }

    for (JBL_NODE cc = caps_codecs->child; cc; cc = cc->next) {
      int rv = jbn_paths_compare(cc, "/preferredPayloadType", consumable_codec, "/mappedPayloadType", JBV_I64, &rc);
      RCGO(rc, finish);
      if (!rv) {
        matched_cap_codec = cc;
        break;
      }
    }
    if (!matched_cap_codec) {
      rc = IW_ERROR_ASSERTION;
      iwlog_ecode_error3(rc);
      goto finish;
    }

    RCC(rc, finish, jbn_from_json("{}", &consumable_codec, pool));

    RCC(rc, finish, jbn_copy_paths(matched_cap_codec,
                                   consumable_codec,
                                   (const char*[]) {
      "/mimeType", "/clockRate", "/channels", "/rtcpFeedback", 0
    },
                                   true, false, pool));

    RCC(rc, finish, jbn_copy_path(matched_cap_codec, "/preferredPayloadType", \
                                  consumable_codec, "/payloadType",
                                  true, false, pool));

    RCC(rc, finish, jbn_copy_path(pc, "/parameters",
                                  consumable_codec, "/parameters",
                                  true, false, pool));

    jbn_add_item(consumable_codecs, consumable_codec);

    for (JBL_NODE cc = caps_codecs->child; cc; cc = cc->next) {
      if (rct_codec_is_rtx(cc)) {
        int rv = jbn_paths_compare(cc, "/parameters/apt", consumable_codec, "/payloadType", JBV_I64, &rc);
        RCGO(rc, finish);
        if (!rv) {
          consumable_cap_rtx_codec = cc;
          break;
        }
      }
    }

    if (consumable_cap_rtx_codec) {
      RCC(rc, finish, jbn_from_json("{}", &consumable_rtx_codec, pool));
      RCC(rc, finish, jbn_copy_paths(consumable_cap_rtx_codec,
                                     consumable_rtx_codec,
                                     (const char*[]) {
        "/mimeType", "/clockRate", "/parameters", "/rtcpFeedback", 0
      }, true, false, pool));
      RCC(rc, finish, jbn_copy_path(consumable_cap_rtx_codec, "/preferredPayloadType",
                                    consumable_rtx_codec, "/payloadType",
                                    true, false, pool));
      jbn_add_item(consumable_codecs, consumable_rtx_codec);
    }
  }

  // headerExtensions
  for (JBL_NODE ext = caps_header_extensions->child; ext; ext = ext->next) {
    RCC(rc, finish, jbn_at(ext, "/kind", &n1));
    RCC(rc, finish, jbn_at(ext, "/direction", &n2));
    if ((n1->type != JBV_STR) || (n2->type != JBV_STR)) {
      continue;
    }
    if (  (strncmp(kind, n1->vptr, n1->vsize) != 0)
       || (  (strncmp("sendrecv", n2->vptr, n2->vsize) != 0)
          && (strncmp("sendonly", n2->vptr, n2->vsize) != 0))) {
      continue;
    }
    RCC(rc, finish, jbn_from_json("{\"parameters\":{}}", &n1, pool));
    RCC(rc, finish, jbn_copy_path(ext, "/uri", n1, "/uri", true, false, pool));
    RCC(rc, finish, jbn_copy_path(ext, "/preferredId", n1, "/id", true, false, pool));
    RCC(rc, finish, jbn_copy_path(ext, "/preferredEncrypt", n1, "/encrypt", true, false, pool));

    jbn_add_item(consumable_header_extensions, n1);
  }

  // Clone Producer encodings since we'll mangle them.
  int i = 0;
  for (JBL_NODE encoding = params_encodings->child; encoding; encoding = encoding->next, ++i) {
    char buf[64];
    snprintf(buf, sizeof(buf), "/encodings/%d/mappedSsrc", i);
    RCC(rc, finish, jbn_at(rtp_mapping, buf, &n1));
    if (n1->type != JBV_I64) {
      rc = IW_ERROR_ASSERTION;
      iwlog_ecode_error3(rc);
      goto finish;
    }
    RCC(rc, finish, jbn_clone(encoding, &n2, pool));
    jbn_detach(n2, "/rid");
    jbn_detach(n2, "/rtx");
    jbn_detach(n2, "/ssrc");
    jbn_detach(n2, "/codecPayloadType");
    jbn_add_item_i64(n2, "ssrc", n1->vi64, 0, pool);
    jbn_add_item(consumable_encodings, n2);
  }

  RCC(rc, finish, jbn_copy_path(rtp_parameters, "/rtcp/cname", consumable_rtcp, "/cname", true, false, pool));
  RCC(rc, finish, jbn_add_item_bool(consumable_rtcp, "reducedSize", true, 0, pool));
  RCC(rc, finish, jbn_add_item_bool(consumable_rtcp, "mux", true, 0, pool));

  // Data
  RCC(rc, finish, jbn_from_json("{}", &data, pool));
  RCC(rc, finish, jbn_add_item_str(data, "kind", kind, strlen(kind), 0, pool));
  RCC(rc, finish, jbn_copy_path(rtp_parameters, "/", data, "/rtpParameters", true, false, pool));
  RCC(rc, finish, jbn_copy_path(rtp_mapping, "/", data, "/rtpMapping", true, false, pool));
  RCC(rc, finish, jbn_add_item_i64(data, "keyFrameRequestDelay", producer->spec->key_frame_request_delay, 0, pool));
  RCC(rc, finish, jbn_add_item_bool(data, "paused", producer->spec->paused, 0, pool));
  RCC(rc, finish, jbl_from_node(&input->data, data));

  // Save consumable rtp parameters
  RCC(rc, finish, jbn_clone(consumable_rtp_parameters, &spec->consumable_rtp_parameters, spec->pool));

finish:
  iwstree_destroy(map_codec2cap);
  iwpool_destroy(pool);
  return rc;
}

iwrc rct_producer_create(wrc_resource_t transport_id, rct_producer_spec_t *spec, wrc_resource_t *producer_out) {
  if (!spec || !producer_out || !spec->rtp_parameters) {
    return IW_ERROR_INVALID_ARGS;
  }
  *producer_out = 0;

  iwrc rc = 0, rc2;
  wrc_msg_t *m = 0;
  rct_transport_t *transport = 0;
  JBL jbl = 0;
  IWPOOL *pool = spec->pool;
  JBL_NODE n, rtp_parameters = spec->rtp_parameters;
  bool locked = false;

  rct_producer_t *producer = iwpool_calloc(sizeof(*producer), pool);
  RCA(producer, finish);
  producer->pool = spec->pool;
  producer->type = RCT_TYPE_PRODUCER;
  producer->spec = spec;
  producer->paused = spec->paused;
  iwu_uuid4_fill(producer->uuid);

  transport = rct_resource_by_id_locked(transport_id, RCT_TYPE_TRANSPORT_ALL, __func__), locked = true;
  RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  spec = 0;
  rct_resource_ref_lk(producer, RCT_INIT_REFS, __func__);
  producer->transport = rct_resource_ref_lk(transport, 1, __func__);

  if (transport->type != RCT_TYPE_TRANSPORT_PIPE) {
    if (!transport->cname_for_producers) {
      rc2 = jbn_at(rtp_parameters, "/rtcp/cname", &n);
      if (!rc2 && (n->type == JBV_STR)) {
        transport->cname_for_producers = n->vptr;
      }
    }
    if (!transport->cname_for_producers) {
      RCB(finish, transport->cname_for_producers = iwpool_alloc(IW_UUID_STR_LEN + 1, pool));
      iwu_uuid4_fill((void*) transport->cname_for_producers);
      *((char*) transport->cname_for_producers + IW_UUID_STR_LEN) = '\0';
      rc2 = jbn_at(rtp_parameters, "/rtcp/cname", &n);
      if (rc2) {
        rc = jbn_copy_path(&(struct _JBL_NODE) {
          .type = JBV_STR,
          .vsize = IW_UUID_STR_LEN,
          .vptr = transport->cname_for_producers
        }, "/", rtp_parameters, "/rtcp/cname", false, false, pool);
        RCGO(rc, finish);
      } else {
        n->type = JBV_STR;
        n->vsize = IW_UUID_STR_LEN;
        n->vptr = transport->cname_for_producers;
        n->child = 0;
      }
    }
  }

  // Prepare transport.produce message
  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = transport->router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_TRANSPORT_PRODUCE
      }
    }
  }));
  RCC(rc, finish, jbl_clone(transport->identity, &m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "producerId", producer->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &producer->identity, pool));
  RCC(rc, finish, _rct_transport_produce_input(producer, &m->input.worker));

  // Register producer
  rct_producer_base_t *pp = transport->producers;
  transport->producers = (void*) producer;
  producer->next = pp;

  producer->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(producer));
  rct_resource_unlock_keep_ref(transport), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  if (m->output.worker.data) {
    rc2 = jbl_at(m->output.worker.data, "/data/type", &jbl);
    const char *v = !rc2 && jbl_type(jbl) == JBV_STR ? jbl_get_str(jbl) : 0;
    if (v) {
      if (strcmp("simple", v) == 0) {
        producer->producer_type = RCT_PRODUCER_SIMPLE;
        producer->producer_type_str = "simple";
      } else if (strcmp("simulcast", v) == 0) {
        producer->producer_type = RCT_PRODUCER_SIMULCAST;
        producer->producer_type_str = "simulcast";
      } else if (strcmp("svc", v) == 0) {
        producer->producer_type = RCT_PRODUCER_SVC;
        producer->producer_type_str = "svc";
      } else {
        rc = IW_ERROR_ASSERTION;
        iwlog_error("RCT Invalid producer type received: %s", v);
        goto finish;
      }
    }
  } else {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  wrc_notify_event_handlers(WRC_EVT_PRODUCER_CREATED, producer->id, 0);
  *producer_out = producer->id;

finish:
  rct_resource_ref_keep_locking(producer, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(transport, locked, -1, __func__);
  wrc_msg_destroy(m);
  jbl_destroy(&jbl);
  if (spec) { // Spec ownership not transfered to producer
    iwpool_destroy(spec->pool);
  }
  return rc;
}

iwrc rct_producer_dump(wrc_resource_t producer_id, JBL *dump_out) {
  return rct_resource_json_command(producer_id, WRC_CMD_PRODUCER_DUMP, RCT_TYPE_PRODUCER, 0, dump_out);
}

iwrc rct_producer_stats(wrc_resource_t producer_id, JBL *result_out) {
  rct_resource_base_t b;
  RCR(rct_resource_probe_by_id(producer_id, &b));
  if (b.type == RCT_TYPE_PRODUCER) {
    return rct_resource_json_command(producer_id, WRC_CMD_PRODUCER_GET_STATS, b.type, 0, result_out);
  } else if (b.type == RCT_TYPE_PRODUCER_DATA) {
    return rct_resource_json_command(producer_id, WRC_CMD_DATA_PRODUCER_GET_STATS, b.type, 0, result_out);
  } else {
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
}

static void _set_paused_state(wrc_resource_t producer_id, bool paused) {
  rct_lock();
  rct_producer_t *p = rct_resource_by_id_unsafe(producer_id, RCT_TYPE_PRODUCER_ALL);
  if (p) {
    p->paused = paused;
  } else {
    rct_unlock();
    return;
  }
  rct_unlock();
  if (paused) {
    wrc_notify_event_handlers(WRC_EVT_PRODUCER_PAUSE, producer_id, 0);
  } else {
    wrc_notify_event_handlers(WRC_EVT_PRODUCER_RESUME, producer_id, 0);
  }
}

iwrc rct_producer_pause(wrc_resource_t producer_id) {
  iwrc rc = rct_resource_json_command(producer_id, WRC_CMD_PRODUCER_PAUSE, RCT_TYPE_PRODUCER, 0, 0);
  if (!rc) {
    _set_paused_state(producer_id, true);
  }
  return rc;
}

iwrc rct_producer_resume(wrc_resource_t producer_id) {
  iwrc rc = rct_resource_json_command(producer_id, WRC_CMD_PRODUCER_RESUME, RCT_TYPE_PRODUCER, 0, 0);
  if (!rc) {
    _set_paused_state(producer_id, false);
  }
  return rc;
}

iwrc rct_producer_pause_async(wrc_resource_t producer_id) {
  iwrc rc = rct_resource_json_command_async(producer_id, WRC_CMD_PRODUCER_PAUSE, RCT_TYPE_PRODUCER, 0);
  if (!rc) {
    _set_paused_state(producer_id, true);
  }
  return rc;
}

iwrc rct_producer_resume_async(wrc_resource_t producer_id) {
  iwrc rc = rct_resource_json_command_async(producer_id, WRC_CMD_PRODUCER_RESUME, RCT_TYPE_PRODUCER, 0);
  if (!rc) {
    _set_paused_state(producer_id, false);
  }
  return rc;
}

iwrc rct_producer_enable_trace_events(wrc_resource_t producer_id, uint32_t events) {
  JBL jbl = 0, jbl2 = 0;
  iwrc rc = jbl_create_empty_object(&jbl);
  RCGO(rc, finish);
  RCC(rc, finish, jbl_create_empty_array(&jbl2));

  if (events & PRODUCER_TRACE_EVENT_RTP) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "rtp"));
  }
  if (events & PRODUCER_TRACE_EVENT_KEYFRAME) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "keyframe"));
  }
  if (events & PRODUCER_TRACE_EVENT_NACK) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "nack"));
  }
  if (events & PRODUCER_TRACE_EVENT_PLI) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "pli"));
  }
  if (events & PRODUCER_TRACE_EVENT_FIR) {
    RCC(rc, finish, jbl_set_string(jbl2, 0, "fir"));
  }

  RCC(rc, finish, jbl_set_nested(jbl, "types", jbl2));
  rc = rct_resource_json_command(producer_id, WRC_CMD_PRODUCER_ENABLE_TRACE_EVENT, 0, jbl, 0);

finish:
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  return rc;
}
