#include "grh_room.h"
#include "grh_auth.h"
#include "gr_task_worker.h"

#include <ejdb2/ejdb2.h>
#include <iowow/iwuuid.h>

#include <string.h>

#define ACTIVITY_PARTICIPADED 0x01
#define ACTIVITY_HASREC       0x02
#define ACTIVITY_ROOM_CLOSED  0x04

static iwrc _activity(
  IWPOOL  *pool,
  int64_t  user_id,
  JBL_NODE n_room,
  JBL_NODE out_activity,
  JBL_NODE out_chat,
  int     *out_activity_flags
  ) {
  *out_activity_flags = 0;

  JBL_NODE n_events;
  iwrc rc = jbn_at(n_room, "/events", &n_events);
  if (rc || n_events->type != JBV_ARRAY) {
    return 0;
  }
  for (JBL_NODE n = n_events->child; n; n = n->next) {
    JBL_NODE n_cv, n_name, n_ts, n_entry;
    if (n->type != JBV_ARRAY) {
      continue;
    }
    n_cv = n->child;
    if (!n_cv || n_cv->type != JBV_STR || !(  strcmp("created", n_cv->vptr) == 0
                                           || strcmp("closed", n_cv->vptr) == 0
                                           || strcmp("renamed", n_cv->vptr) == 0
                                           || strcmp("joined", n_cv->vptr) == 0
                                           || strcmp("left", n_cv->vptr) == 0
                                           || strcmp("recstart", n_cv->vptr) == 0
                                           || strcmp("recstop", n_cv->vptr) == 0
                                           || strcmp("message", n_cv->vptr) == 0
                                           || strcmp("whiteboard", n_cv->vptr) == 0)) {
      continue;
    }
    n_name = n_cv;
    n_cv = n_cv->next;
    if (!n_cv || n_cv->type != JBV_I64) {
      continue;
    }
    n_ts = n_cv;
    RCC(rc, finish, jbn_from_json("{}", &n_entry, pool));

    if (strcmp("message", n_name->vptr) != 0) {
      RCC(rc, finish, jbn_add_item_str(n_entry, "event", n_name->vptr, n_name->vsize, 0, pool));
    }
    RCC(rc, finish, jbn_add_item_i64(n_entry, "ts", n_ts->vi64, 0, pool));
    if (strcmp("closed", n_name->vptr) == 0) {
      *out_activity_flags |= ACTIVITY_ROOM_CLOSED;
      jbn_add_item(out_activity, n_entry);
      continue;
    }
    if (strcmp("created", n_name->vptr) == 0) {
      jbn_add_item(out_activity, n_entry);
      continue;
    }
    if (strcmp("renamed", n_name->vptr) == 0) {
      jbn_add_item(out_activity, n_entry);
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "old_name", n_cv->vptr, n_cv->vsize, 0, pool));
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "new_name", n_cv->vptr, n_cv->vsize, 0, pool));
      continue;
    }
    if (strncmp("rec", n_name->vptr, 3) == 0) {
      jbn_add_item(out_activity, n_entry);
      *out_activity_flags |= ACTIVITY_HASREC;
      continue;
    }
    if (strcmp("whiteboard", n_name->vptr) == 0) {
      jbn_add_item(out_activity, n_entry);
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "member", n_cv->vptr, n_cv->vsize, 0, pool));
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "link", n_cv->vptr, n_cv->vsize, 0, pool));
      continue;
    }

    n_cv = n_cv->next;
    if (!n_cv || n_cv->type != JBV_I64) {
      continue;
    }
    if (strcmp("joined", n_name->vptr) == 0) {
      if (n_cv->vi64 == user_id) {
        *out_activity_flags |= ACTIVITY_PARTICIPADED;
      }
      n_cv = n_cv->next; // uuid
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      n_cv = n_cv->next; // member_name
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "member", n_cv->vptr, n_cv->vsize, 0, pool));
      jbn_add_item(out_activity, n_entry);
    } else if (strcmp("left", n_name->vptr) == 0) {
      n_cv = n_cv->next; // member_name
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "member", n_cv->vptr, n_cv->vsize, 0, pool));
      jbn_add_item(out_activity, n_entry);
    } else if (strcmp("message", n_name->vptr) == 0) {
      RCC(rc, finish, jbn_add_item_bool(n_entry, "own", n_cv->vi64 == user_id, 0, pool));
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "member", n_cv->vptr, n_cv->vsize, 0, pool));
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_I64) {
        continue;
      }
      n_cv = n_cv->next;
      if (!n_cv || n_cv->type != JBV_STR) {
        continue;
      }
      RCC(rc, finish, jbn_add_item_str(n_entry, "message", n_cv->vptr, n_cv->vsize, 0, pool));
      jbn_add_item(out_chat, n_entry);
    }
  }

finish:
  return rc;
}

static void _rec(IWPOOL *pool, int activity_flags, const char *ref, EJDB_DOC room, JBL_NODE out_rec) {
  iwrc rc = 0;
  JBL_NODE n;
  JQL q = 0;
  EJDB_LIST list = 0;

  if (!(activity_flags & ACTIVITY_HASREC)) {
    return;
  }

  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);
  RCC(rc, finish, iwxstr_printf(xstr, "/rec/%s", ref));
  jbn_add_item_str(out_rec, "url", iwxstr_ptr(xstr), -1, 0, pool);

  if (!(activity_flags & ACTIVITY_ROOM_CLOSED)) {
    // Room is not yet closed so pending state
    jbn_add_item_str(out_rec, "status", "pending", sizeof("pending") - 1, 0, pool);
    goto finish;
  }
  rc = jbn_at(room->node, "/recf", &n);
  if (!rc && n->type == JBV_STR) {
    jbn_add_item_str(out_rec, "status", "success", sizeof("success") - 1, 0, pool);
    goto finish;
  }

  // Try to find status of last task
  RCC(rc, finish, jql_create(&q, "tasks", "/[type = :?] and /[hook = :?] | desc /ts"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, PT_RECORDING_POSTPROC));
  RCC(rc, finish, jql_set_str(q, 0, 1, ref));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));
  if (list->first) {
    int64_t status = 0;
    RCC(rc, finish, jbl_object_get_i64(list->first->raw, "status", &status));
    if (status == 0) {
      // Task is not started or in progress
      jbn_add_item_str(out_rec, "status", "pending", sizeof("pending") - 1, 0, pool);
    } else if (status == -1) {
      jbn_add_item_str(out_rec, "status", "failed", sizeof("failed") - 1, 0, pool);
    }
  } else {
    jbn_add_item_str(out_rec, "status", "pending", sizeof("pending") - 1, 0, pool);
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  iwxstr_destroy(xstr);
}

static void _history(int64_t user_id, const char *uuid, JBL_NODE out_history, IWPOOL *pool) {
  JQL q = 0;
  EJDB_LIST list = 0;
  char prefix[NUMBUSZ + 1];
  snprintf(prefix, sizeof(prefix), "%" PRId64 ":", user_id);

  iwrc rc = jql_create(&q, "joins", "/[k ~ :?] and /[u = :?] | desc /t");
  RCGO(rc, finish);
  RCC(rc, finish, jql_set_str(q, 0, 0, prefix));
  RCC(rc, finish, jql_set_str(q, 0, 1, uuid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, g_env.room.max_history_sessions, 0, &list));

  for (EJDB_DOC doc = list->first; doc; doc = doc->next) {
    int64_t ts = 0;
    const char *k = 0;
    jbl_object_get_str(doc->raw, "k", &k);
    jbl_object_get_i64(doc->raw, "t", &ts);
    if (k && ts && (k = strrchr(k, ':'))) {
      k++;
      JBL_NODE n;
      RCC(rc, finish, jbn_from_json("[]", &n, pool));
      RCC(rc, finish, jbn_add_item_str(n, 0, k, -1, 0, pool));
      RCC(rc, finish, jbn_add_item_i64(n, 0, ts, 0, pool));
      jbn_add_item(out_history, n);
    }
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
}

static int _handler_info(struct iwn_wf_req *req, void *d) {
  iwrc rc = 0;
  int ret = 500;
  JQL q = 0;
  EJDB_DOC room = 0;
  int64_t user_id = 0;
  int activity_flags = 0;
  JBL_NODE n_db_name, n_db_uuid, n_db_cid;
  JBL_NODE n_resp = 0, n_name, n_activity, n_chat, n_rec, n_hist;

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    rc = iwrc_set_errno(rc, IW_ERROR_ALLOC);
    goto finish;
  }
  struct iwn_val ref = iwn_pair_find_val(&req->query_params, "ref", IW_LLEN("ref"));
  if (ref.len == 0) {
    ret = 400;
    goto finish;
  }

  RCC(rc, finish, jbn_from_json(
        "{\"name\":\"\", \"activity\":[], \"chat\":[], \"recording\":{}, \"history\":[]}",
        &n_resp, pool));

  if ((user_id = grh_auth_get_userid(req)) == 0) {
    goto finish;
  }

  RCC(rc, finish, jbn_at(n_resp, "/name", &n_name));
  RCC(rc, finish, jbn_at(n_resp, "/activity", &n_activity));
  RCC(rc, finish, jbn_at(n_resp, "/chat", &n_chat));
  RCC(rc, finish, jbn_at(n_resp, "/recording", &n_rec));
  RCC(rc, finish, jbn_at(n_resp, "/history", &n_hist));

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] or /[cid = :?] | /{name,uuid,cid,events,recf}"));
  RCC(rc, finish, jql_set_str(q, 0, 0, ref.buf));
  RCC(rc, finish, jql_set_str(q, 0, 1, ref.buf));
  RCC(rc, finish, ejdb_list(g_env.db, q, &room, 1, pool));
  if (!room) {
    iwlog_warn("Unable to find room by cid: %s", ref.buf);
    goto finish;
  }

  RCC(rc, finish, _activity(pool, user_id, room->node, n_activity, n_chat, &activity_flags));
  if (!(activity_flags & ACTIVITY_PARTICIPADED)) {
    // Room may be a webinar where `join events` of participants are not stored in the room document.
    // Although join event will be always recorded into `joins` collection.
    // So use grh_auth_is_room_member() to find the fact of room participation.
    bool owner;
    if (grh_auth_is_room_member(ref.buf, user_id, &owner)) {
      activity_flags |= ACTIVITY_PARTICIPADED;
    }
  }

  if (!(activity_flags & ACTIVITY_PARTICIPADED) && !grh_auth_has_any_perms(req, "admin")) {
    // Reset response to empty
    RCC(rc, finish, jbn_from_json(
          "{\"name\": \"\", \"activity\":[], \"chat\":[], \"recording\":{}, \"history\":[]}",
          &n_resp, pool));
    goto finish;
  }

  if (!jbn_at(room->node, "/name", &n_db_name) && n_db_name->type == JBV_STR) {
    n_name->vptr = n_db_name->vptr;
    n_name->vsize = n_db_name->vsize;
  }

  RCC(rc, finish, jbn_at(room->node, "/uuid", &n_db_uuid));
  RCC(rc, finish, jbn_at(room->node, "/cid", &n_db_cid));
  if (n_db_uuid->type == n_db_cid->type && n_db_uuid->type == JBV_STR) {
    const char *uuid = n_db_uuid->vptr;
    if (strcmp(uuid, ref.buf) == 0) {
      uuid = n_db_cid->vptr;
    }
    _history(user_id, uuid, n_hist, pool);
  }

  _rec(pool, activity_flags, ref.buf, room, n_rec);

  ret = iwn_http_response_jbn(req->http, n_resp, 200, true);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
  return ret;
}

iwrc grh_route_room(struct iwn_wf_route *parent) {
  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/room/info",
    .handler = _handler_info,
  }, 0);
}
