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

#include "grh_ws.h"
#include "grh_ws_user.h"
#include "grh_auth.h"
#include "wrc/wrc.h"
#include "rct/rct.h"

#include <ejdb2/ejdb2.h>
#include <ejdb2/jql.h>
#include <ejdb2/jbl.h>
#include <ejdb2/iowow/iwp.h>

/// Remove room history item
static iwrc _history_rooms_remove(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
        room: room uuid
     }
   */
  iwrc rc = 0;
  JBL_NODE n, n2, n_rooms;
  EJDB_LIST res = 0;
  const char *error = 0, *room_uuid;
  JQL q = 0;
  int64_t user_id = grh_auth_get_userid(ctx->wss->ws->req);

  if (!ctx->payload) {
    rc = GR_ERROR_COMMAND_INVALID_INPUT;
    goto finish;
  }
  RCC(rc, finish, jbn_at(ctx->payload, "/room", &n));
  if (!n || n->type != JBV_STR) {
    rc = GR_ERROR_COMMAND_INVALID_INPUT;
    goto finish;
  }
  room_uuid = n->vptr;

  RCC(rc, finish, jql_create(&q, "users", "/= :? | /rooms"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, user_id));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &res));

  if (!res->first) {
    goto finish;
  }
  rc = jbn_at(res->first->node, "/rooms", &n_rooms);
  if (rc || n_rooms->type != JBV_ARRAY) {
    rc = 0;
    goto finish;
  }

  for (n = n_rooms->child; n; n = n->next) {
    rc = jbn_at(n, "/uuid", &n2);
    if (rc == 0 && n2->type == JBV_STR && strcmp(n2->vptr, room_uuid) == 0) {
      jbn_remove_item(n_rooms, n);
    }
  }

  RCC(rc, finish, ejdb_patch_jbn(g_env.db, "users", res->first->node, user_id));

finish:
  SIMPLE_HANDLER_FINISH_RC(0);
  jql_destroy(&q);
  ejdb_list_destroy(&res);
  return rc;
}

static void _on_room_member_join(wrc_resource_t member_id) {
  iwrc rc = 0;
  JQL q = 0;
  int64_t user_id = 0;
  uint64_t ts;
  char room_uuid[IW_UUID_STR_LEN + 1];
  char room_cid[IW_UUID_STR_LEN + 1];
  const char *room_name;

  bool locked = false;
  int i = 0;

  JBL_NODE n_rooms, n, n2;
  EJDB_DOC doc;
  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return;
  }

  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  locked = true;
  if (!member) {
    goto finish;
  }
  user_id = member->user_id;

  memcpy(room_uuid, member->room->uuid, IW_UUID_STR_LEN);
  room_uuid[IW_UUID_STR_LEN] = '\0';

  memcpy(room_cid, member->room->cid, IW_UUID_STR_LEN);
  room_cid[IW_UUID_STR_LEN] = '\0';

  room_name = iwpool_strdup(pool, member->room->name, &rc);
  rct_resource_unlock(member, __func__), locked = false;
  RCGO(rc, finish);

  RCC(rc, finish, jql_create(&q, "users", "/= :? | /rooms"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, user_id));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 1, pool));

  if (!doc) {
    goto finish;
  }
  rc = jbn_at(doc->node, "/rooms", &n_rooms);
  if (rc || n_rooms->type != JBV_ARRAY) {
    if (rc == 0) {
      jbn_remove_item(doc->node, n_rooms);
    }
    RCC(rc, finish, jbn_add_item_arr(doc->node, "rooms", &n_rooms, pool));
  }
  for (n = n_rooms->child; n; n = n->next, rc = 0, ++i) {
    rc = jbn_at(n, "/uuid", &n2);
    if (rc == 0 && n2->type == JBV_STR && strcmp(n2->vptr, room_uuid) == 0) {
      jbn_remove_item(n_rooms, n);
      break;
    }
    if (i >= g_env.room.max_history_rooms - 1) {
      // Trim the rooms list
      n->next = 0;
      break;
    }
  }
  RCC(rc, finish, jbn_from_json("{}", &n, pool));
  RCC(rc, finish, jbn_add_item_str(n, "uuid", room_uuid, -1, 0, pool));
  RCC(rc, finish, jbn_add_item_str(n, "cid", room_cid, -1, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "ts", (int64_t) ts, 0, pool));
  RCC(rc, finish, jbn_add_item_str(n, "name", room_name, -1, 0, pool));

  // Insert history node at begining of the array
  if (n_rooms->child) {
    JBL_NODE tmp = n_rooms->child;
    n->next = tmp;
    tmp->prev = n;
    n_rooms->child = n;
    // Reindex array elements
    for (i = 0, n = n_rooms->child; n; n = n->next, ++i) {
      n->klidx = i;
    }
  } else {
    n_rooms->child = n;
  }

  // Now patch document
  RCC(rc, finish, ejdb_patch_jbn(g_env.db, "users", doc->node, user_id));

finish:
  if (locked) {
    rct_resource_unlock(member, __func__);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static iwrc _rct_event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  switch (evt) {
    case WRC_EVT_ROOM_MEMBER_JOIN:
      _on_room_member_join(resource_id);
      break;
    default:
      break;
  }
  return 0;
}

static uint32_t _event_handler_id;

void grh_ws_user_close(void) {
  wrc_remove_event_handler(_event_handler_id);
}

iwrc grh_ws_user_init(void) {
  iwrc rc = 0;
  RCC(rc, finish, grh_ws_register_wsh_handler("history_rooms_remove",
                                              "grh_ws_user::history_rooms_remove", _history_rooms_remove, 0, 0));
  RCC(rc, finish, wrc_add_event_handler(_rct_event_handler, 0, &_event_handler_id));

finish:
  return rc;
}
