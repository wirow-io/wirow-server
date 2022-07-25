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

#include "rct_router.h"

#include <iowow/iwuuid.h>
#include <iowow/iwarr.h>

#include <string.h>

static int initial_payloads[] = {
  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
  111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
  122, 123, 124, 125, 126, 127, 96,  97,  98,  99
};

void rct_router_close_lk(rct_router_t *r) {
  if (r->room) {
    r->room->close_pending = true;
    rct_resource_close_lk(r->room);
    r->room = 0;
  }
  for (rct_transport_t *p = r->transports, *n; p; p = n) {
    n = p->next;
    rct_resource_close_lk(p);
  }
  for (rct_rtp_observer_t *p = r->observers, *n; p; p = n) {
    n = p->next;
    rct_resource_close_lk(p);
  }
}

static iwrc _rct_router_create_rt_capabilities(JBL_NODE *caps_out, IWPOOL *pool) {
  *caps_out = 0;

  iwrc rc;
  const char *mime_type;
  int64_t preferred_payload_type, clock_rate;

  JBL_NODE n1, n2;
  JBL_NODE available_caps = state.available_capabilities, available_codecs;
  JBL_NODE user_caps, user_codecs;
  JBL_NODE result_caps, result_codecs, result_codec;

  IWULIST payloads = { 0 };
  int len = sizeof(initial_payloads) / sizeof(initial_payloads[0]);
  RCC(rc, finish, iwulist_init(&payloads, len, sizeof(initial_payloads[0])));

  for (int i = 0; i < len; ++i) {
    RCC(rc, finish, iwulist_push(&payloads, &initial_payloads[i]));
  }

  RCC(rc, finish, jbn_from_json("{\"codecs\":[]}", &result_caps, pool));
  RCC(rc, finish, jbn_from_json(g_env.router_optons_json, &user_caps, pool));
  RCC(rc, finish, jbn_at(user_caps, "/codecs", &user_codecs));
  RCC(rc, finish, jbn_at(available_caps, "/codecs", &available_codecs));
  RCC(rc, finish, jbn_at(result_caps, "/codecs", &result_codecs));

  for (JBL_NODE uc = user_codecs->child; uc; uc = uc->next) {
    for (JBL_NODE ac = available_codecs->child; ac; ac = ac->next) {
      if ((ac->type != JBV_OBJECT) || (uc->type != JBV_OBJECT)) {
        continue;
      }
      // Compare mime types
      rc = jbn_at(uc, "/mimeType", &n1);
      if (rc || (n1->type != JBV_STR)) {
        continue;
      }
      rc = jbn_at(ac, "/mimeType", &n2);
      if (rc || (n2->type != n1->type) || (n2->vsize != n1->vsize) || utf8ncasecmp(n1->vptr, n2->vptr, n1->vsize)) {
        continue;
      }
      mime_type = n1->vptr;

      // Compare clock rates
      rc = jbn_at(uc, "/clockRate", &n1);
      if (rc || (n1->type != JBV_I64)) {
        continue;
      }
      rc = jbn_at(ac, "/clockRate", &n2);
      if (rc || (n2->type != n1->type) || (n2->vi64 != n1->vi64)) {
        continue;
      }
      clock_rate = n1->vi64;

      // Compare channels
      int64_t iv1 = 1, iv2 = iv1;
      rc = jbn_at(uc, "/channels", &n1);
      if (!rc && (n1->type == JBV_I64)) {
        iv1 = n1->vi64;
      }
      rc = jbn_at(ac, "/channels", &n2);
      if (!rc && (n2->type == JBV_I64)) {
        iv2 = n2->vi64;
      }
      if (iv1 != iv2) {
        continue;
      }

      // Checks specific to some codecs
      if (!strcasecmp(mime_type, "video/h264")) {
        iv1 = 0, iv2 = 0;
        rc = jbn_at(uc, "/parameters/packetization-mode", &n1);
        if (!rc && (n1->type == JBV_I64)) {
          iv1 = n1->vi64;
        }
        rc = jbn_at(ac, "/parameters/packetization-mode", &n2);
        if (!rc && (n2->type == JBV_I64)) {
          iv2 = n2->vi64;
        }
        if (iv1 != iv2) {
          continue;
        }
      }
      RCC(rc, finish, jbn_clone(ac, &result_codec, pool));
      RCC(rc, finish, jbn_patch_auto(result_codec, uc, pool));

      // Merge payload types
      iv1 = 0, iv2 = 0;
      n1 = 0, n2 = 0;
      rc = jbn_at(uc, "/preferredPayloadType", &n1);
      if (!rc && (n1->type == JBV_I64)) {
        iv1 = n1->vi64;
      }
      rc = jbn_at(result_codec, "/preferredPayloadType", &n2);
      if (!rc && (n2->type == JBV_I64)) {
        iv2 = n2->vi64;
      }
      if (iv1) {
        for (int i = 0, l = iwulist_length(&payloads); i < l; ++i) {
          int *pp = iwulist_at2(&payloads, i);
          if (*pp == iv1) {
            iwulist_remove(&payloads, i);
            break;
          }
        }
      } else if (!iv2) {
        if (iwulist_length(&payloads)) {
          int *pp = iwulist_at2(&payloads, 0);
          iv2 = *pp;
          iwulist_shift(&payloads);
          if (!n2) {
            RCC(rc, finish, jbn_add_item_obj(result_codec, "preferredPayloadType", &n2, pool));
          }
          n2->type = JBV_I64;
          n2->vi64 = iv2;
        } else {
          rc = RCT_ERROR_TOO_MANY_DYNAMIC_PAYLOADS;
          goto finish;
        }
      }
      preferred_payload_type = iv2;

      jbn_add_item(result_codecs, result_codec);

      rc = jbn_at(result_codec, "/kind", &n1);
      if (rc || (n1->type != JBV_STR)) {
        continue;
      }
      if (!strncmp(n1->vptr, "video", n1->vsize)) { // Register RTX codec
        int pt;
        if (iwulist_length(&payloads)) {
          int *pp = iwulist_at2(&payloads, 0);
          pt = *pp;
          iwulist_shift(&payloads);
        } else {
          rc = RCT_ERROR_TOO_MANY_DYNAMIC_PAYLOADS;
          goto finish;
        }

        JBL_NODE rxt = iwpool_calloc(sizeof(*rxt), pool);
        RCA(rxt, finish);
        rxt->type = JBV_OBJECT;

        RCC(rc, finish, jbn_add_item_str(rxt, "kind", "video", -1, 0, pool));
        RCC(rc, finish, jbn_add_item_str(rxt, "mimeType", "video/rtx", -1, 0, pool));
        RCC(rc, finish, jbn_add_item_i64(rxt, "preferredPayloadType", pt, 0, pool));
        RCC(rc, finish, jbn_add_item_i64(rxt, "clockRate", clock_rate, 0, pool));
        RCC(rc, finish, jbn_copy_path(&(struct _JBL_NODE) {
          .type = JBV_I64,
          .vi64 = preferred_payload_type
        }, "/", rxt, "/parameters/apt", false, false, pool));
        RCC(rc, finish, jbn_add_item_arr(rxt, "rtcpFeedback", 0, pool));
        jbn_add_item(result_codecs, rxt);
      }
      break; // Process next user codec
    } // eof codec for
  }

  RCC(rc, finish, jbn_copy_path(available_caps, "/headerExtensions",
                                result_caps, "/headerExtensions", false, false, pool));

  // Save result
  *caps_out = result_caps;

finish:
  iwulist_destroy_keep(&payloads);
  return rc;
}

iwrc rct_router_create(
  const char     *uuid,
  wrc_resource_t  worker_id,
  wrc_resource_t *router_id_out
  ) {
  if (!router_id_out || (uuid && (strlen(uuid) != IW_UUID_STR_LEN))) {
    return IW_ERROR_INVALID_ARGS;
  }

  iwrc rc = 0;
  JBL caps = 0;
  wrc_msg_t *m = 0;
  bool locked = false;
  rct_router_t *router = 0;

  IWPOOL *pool = iwpool_create(512);
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  RCB(finish, router = iwpool_calloc(sizeof(*router), pool));
  router->pool = pool;
  router->type = RCT_TYPE_ROUTER;
  if (uuid) {
    memcpy(router->uuid, uuid, sizeof(router->uuid));
  } else {
    iwu_uuid4_fill(router->uuid);
  }
  RCC(rc, finish, _rct_router_create_rt_capabilities(&router->rtp_capabilities, pool));

  if (!worker_id) {
    RCC(rc, finish, rct_worker_acquire_for_router(&router->worker_id));
  } else {
    router->worker_id = worker_id;
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_WORKER_ROUTER_CREATE
      }
    }
  }));
  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "routerId", router->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &router->identity, pool));

  rct_resource_ref_locked(router, RCT_INIT_REFS, __func__), locked = true; // NOLINT clang-analyzer-deadcode.DeadStores
  rc = rct_resource_register_lk(router);
  rct_resource_unlock_keep_ref(router), locked = false;
  RCGO(rc, finish);

  if (g_env.log.verbose) {
    IWXSTR *xstr = iwxstr_new();
    iwxstr_printf(xstr, "RCT Creating router: 0x%" PRIx64
                  " uuid=%s\n", router->id, router->uuid);
    iwlog_info2(iwxstr_ptr(xstr));
    iwxstr_destroy(xstr);
  }

  // Register new router
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  wrc_notify_event_handlers(WRC_EVT_ROUTER_CREATED, router->id, 0);
  *router_id_out = router->id;

finish:
  rct_resource_ref_unlock(router, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  jbl_destroy(&caps);
  wrc_msg_destroy(m);
  return rc;
}

iwrc rct_router_dump(wrc_resource_t router_id, JBL *dump_out) {
  wrc_msg_t *m = 0;
  JBL identity = 0;
  wrc_resource_t worker_id;
  iwrc rc = RCR(rct_resource_get_identity(router_id, RCT_TYPE_ROUTER, &identity, &worker_id));

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = worker_id,
    .input = {
      .worker     = {
        .cmd      = WRC_CMD_ROUTER_DUMP,
        .internal = identity
      }
    }
  }));

  rc = wrc_send_and_wait(m, 0);
  if (rc) {
    *dump_out = 0;
  } else {
    *dump_out = m->output.worker.data;
    m->output.worker.data = 0;
  }

finish:
  wrc_msg_destroy(m);
  return rc;
}

iwrc rct_router_close(wrc_resource_t router_id) {
  iwrc rc = 0;
  JBL identity = 0;
  wrc_msg_t *m = 0;
  wrc_resource_t worker_id = 0;

  rct_router_t *router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__);
  if (router) {
    router->close_pending = true;
    if (router->room) {
      router->room->close_pending = true;
    }
    worker_id = router->worker_id;
    rc = jbl_clone(router->identity, &identity);
  }
  rct_resource_unlock(router, __func__);
  RCRET(rc);
  if (!identity) {
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = worker_id,
    .input = {
      .worker     = {
        .cmd      = WRC_CMD_ROUTER_CLOSE,
        .internal = identity
      }
    }
  }));

  rc = wrc_send_and_wait(m, 0);
  wrc_notify_event_handlers(WRC_EVT_ROUTER_CLOSED, router_id, 0);

finish:
  jbl_destroy(&identity);
  if (m) {
    m->input.worker.internal = 0;
    wrc_msg_destroy(m);
  }
  return rc;
}
