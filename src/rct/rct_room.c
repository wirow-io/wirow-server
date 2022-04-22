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
#include "grh_auth.h"
#include "rct_room_internal.h"
#include "rct_room_recording.h"
#include "rct/rct.h"
#include "rct/rct_router.h"
#include "rct/rct_transport.h"
#include "rct/rct_producer.h"
#include "rct/rct_consumer.h"
#include "rct/rct_observer_al.h"
#include "utils/html.h"
#include "lic/lic.h"

#include <iowow/iwhmap.h>
#include <iowow/iwarr.h>
#include <iowow/iwuuid.h>
#include <iowow/iwpool.h>
#include <iowow/iwstw.h>
#include <iowow/iwutils.h>
#include <iwnet/iwn_scheduler.h>

#include <pthread.h>
#include <assert.h>

#define MRES_SEND_TRANSPORT 0x01U
#define MRES_RECV_TRANSPORT 0x02U

#define CHECK_JBN_TYPE(n__, label__, t__) do { \
    if (!(n__) || (n__)->type != (t__)) { \
      rc = GR_ERROR_COMMAND_INVALID_INPUT; \
      goto label__; \
    } } while (0)

// Map: rct_resource_id => room_member_id
static IWHMAP *_map_resource_member;

struct rct_room_join_spec {
  const char *name;
  char member_uuid[IW_UUID_STR_LEN + 1];
  struct ws_session *wss;
  /**
   * @brief No used currently.
   */
  uint32_t flags;
};

struct rct_room_spec {
  const char *name;
  const char *uuid;        // Optional room uuid
  const char *member_uuid; // Room member uuid;
  struct ws_session *wss;
  /**
   * @brief Room creation flags defined in (rct.h)
   *
   * - RCT_ROOM_MEETING
   * - RCT_ROOM_WEBINAR
   */
  uint32_t flags;
  struct rct_room_join_spec member;
};

struct wss_room_member {
  wrc_resource_t room_member_id;
};

struct send_task {
  wrc_resource_t room_id;
  wrc_resource_t member_id;
  wrc_resource_t exclude_member_id;
  JBL      json;
  JBL_NODE jbn;
  IWPOOL  *pool;
  IWULIST *wsids;
  const char *tag;
};

static void _wss_closed_listener(int event, struct ws_session *wss, void *data);

static void _send_to_task(void *arg) {
  struct send_task *t = arg;
  assert(t);
  iwrc rc = 0;
  uint32_t wsid = 0;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);

  if (t->json) {
    RCC(rc, finish, jbl_as_json(t->json, jbl_xstr_json_printer, xstr, 0));
  } else if (t->jbn) {
    RCC(rc, finish, jbn_as_json(t->jbn, jbl_xstr_json_printer, xstr, 0));
  } else {
    rc = IW_ERROR_INVALID_ARGS;
    goto finish;
  }
  if (t->member_id) {
    rct_room_member_t *m = rct_resource_by_id_locked(t->member_id, RCT_TYPE_ROOM_MEMBER, __func__);
    if (m) {
      wsid = m->wsid;
    }
    rct_resource_unlock(m, __func__);
    if (wsid) {
      grh_ws_send_by_wsid(wsid, iwxstr_ptr(xstr), iwxstr_size(xstr));
    }
  } else if (t->room_id) {
    IWULIST ids;
    iwulist_init(&ids, 64, sizeof(wsid));
    rct_room_t *r = rct_resource_by_id_locked(t->room_id, RCT_TYPE_ROOM, __func__);
    if (r) {
      for (rct_room_member_t *m = r->members; m; m = m->next) {
        if (m->id != t->exclude_member_id) {
          iwulist_push(&ids, &m->wsid);
        }
      }
    }
    rct_resource_unlock(r, __func__);
    for (int i = 0, l = iwulist_length(&ids); i < l; ++i) {
      wsid = *(uint32_t*) iwulist_at2(&ids, i);
      grh_ws_send_by_wsid(wsid, iwxstr_ptr(xstr), iwxstr_size(xstr));
    }
    iwulist_destroy_keep(&ids);
  } else if (t->wsids) {
    for (int i = 0, l = iwulist_length(t->wsids); i < l; ++i) {
      wsid = *(uint32_t*) iwulist_at2(t->wsids, i);
      grh_ws_send_by_wsid(wsid, iwxstr_ptr(xstr), iwxstr_size(xstr));
    }
    iwulist_destroy(&t->wsids);
  } else { // all
    rc = grh_ws_send_all(iwxstr_ptr(xstr), iwxstr_size(xstr));
  }

finish:
  iwxstr_destroy(xstr);
  jbl_destroy(&t->json);
  iwpool_destroy(t->pool);
  if (rc) {
    iwlog_ecode_error(rc, "Send failed, tag: %s", t->tag ? t->tag : "none");
  }
  free(t);
}

static iwrc _send_to_member(wrc_resource_t member_id, JBL json, const char *tag) {
  if (!json) {
    return IW_ERROR_INVALID_ARGS;
  }
  struct send_task *t = malloc(sizeof(*t));
  if (!t) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  *t = (struct send_task) {
    .member_id = member_id,
    .json = json,
    .tag = tag
  };
  iwrc rc = iwtp_schedule(g_env.tp, _send_to_task, t);
  if (rc) {
    free(t);
  }
  return rc;
}

static iwrc _send_to_members(wrc_resource_t room_id, wrc_resource_t exclude_member_id, JBL json, const char *tag) {
  if (!json) {
    return IW_ERROR_INVALID_ARGS;
  }
  struct send_task *t = malloc(sizeof(*t));
  if (!t) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  *t = (struct send_task) {
    .room_id = room_id,
    .exclude_member_id = exclude_member_id,
    .json = json,
    .tag = tag
  };
  iwrc rc = iwtp_schedule(g_env.tp, _send_to_task, t);
  if (rc) {
    free(t);
  }
  return rc;
}

static iwrc _send_by_wsids(IWULIST *wsids, JBL json, const char *tag) {
  if (!json) {
    return IW_ERROR_INVALID_ARGS;
  }
  struct send_task *t = malloc(sizeof(*t));
  if (!t) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  *t = (struct send_task) {
    .json = json,
    .tag = tag,
    .wsids = wsids
  };
  iwrc rc = iwtp_schedule(g_env.tp, _send_to_task, t);
  if (rc) {
    free(t);
  }
  return rc;
}

static iwrc _send_to_members2(wrc_resource_t member_id, JBL json, bool include_member, const char *tag) {
  wrc_resource_t room_id;
  rct_room_member_t *m = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, tag);
  room_id = m ? m->room->id : 0;
  rct_resource_unlock(m, tag);
  if (room_id) {
    return _send_to_members(room_id, include_member ? 0 : member_id, json, tag);
  } else {
    return 0;
  }
}

static iwrc _send_to_all(wrc_resource_t exclude_member_id, JBL json, const char *tag) {
  return _send_to_members(0, exclude_member_id, json, tag);
}

IW_INLINE wrc_resource_t _wss_member_get(struct ws_session *wss) {
  struct grh_user_data *ud = grh_wss_get_data_of_type(wss, GRH_USER_DATA_TYPE_WSROOM_MEMBER);
  if (ud) {
    return (wrc_resource_t) (uintptr_t) ud->data;
  }
  return 0;
}

IW_INLINE iwrc _wss_member_set(struct ws_session *wss, wrc_resource_t room_member_id) {
  iwrc rc = 0;
  struct grh_user_data *d = malloc(sizeof(*d));
  RCB(finish, d);

  *d = (struct grh_user_data) {
    .type = GRH_USER_DATA_TYPE_WSROOM_MEMBER,
    .data = (void*) (uintptr_t) room_member_id
  };
  grh_wss_set_data_of_type(wss, d);

finish:
  return rc;
}

IW_INLINE void _wss_member_unset(struct ws_session *wss) {
  grh_wss_unset_data_of_type(wss, GRH_USER_DATA_TYPE_WSROOM_MEMBER);
}

static void _rct_room_dispose_lk(void *r) {
  assert(r);
  rct_room_t *room = r;
  free(room->name);
#if (ENABLE_WHITEBOARD == 1)
  free(room->whiteboard_link);
#endif
  rct_resource_ref_lk(room->router, -1, __func__); // Unref parent router
}

static void _rct_room_close_lk(void *r) {
  assert(r);
  JBL jbl, jbl2;
  rct_room_t *room = r;

  // Notify all
  if (!jbl_create_empty_object(&jbl)) {
    jbl_set_string(jbl, "event", "ROOM_CLOSED");
    jbl_set_string(jbl, "room", room->uuid);

    jbl_clone(jbl, &jbl2);
    jbl_set_string(jbl2, "cid", room->cid);
    jbl_set_int64(jbl2, "nrs", room->num_recording_sessions);

    wrc_notify_event_handlers(WRC_EVT_ROOM_CLOSED, room->id, jbl2);
    if (room->members) {
      IWULIST *ids = iwulist_create(64, sizeof(room->members->wsid));
      for (rct_room_member_t *m = room->members; ids && m; m = m->next) {
        iwulist_push(ids, &m->wsid);
      }
      if (ids) {
        if (_send_by_wsids(ids, jbl, __func__)) {
          iwulist_destroy(&ids);
          jbl_destroy(&jbl);
        }
      } else {
        jbl_destroy(&jbl);
      }
    } else {
      jbl_destroy(&jbl);
    }
  }

  // Dispose all remaining members
  for (rct_room_member_t *m = room->members, *n; m; m = n) {
    n = m->next;
    rct_resource_close_lk(m);
  }
}

static void _member_close_transports_task(void *op) {
  IWULIST *clist = op;
  assert(clist);
  for (int i = (int) iwulist_length(clist) - 1; i >= 0; --i) {
    wrc_resource_t id = *(wrc_resource_t*) iwulist_at2(clist, i);
    rct_transport_close(id);
  }
  iwulist_destroy(&clist);
}

static void _member_dispose_lk(void *m) {
  assert(m);
  rct_room_member_t *member = m;
  rct_room_t *room = member->room;
  IWULIST *clist = iwulist_create(16, sizeof(wrc_resource_t));

  // Cleanup all resources which point to this room member
  for (int i = (int) iwulist_length(&member->resource_refs) - 1; i >= 0; --i) {
    struct rct_resource_ref *r = iwulist_at2(&member->resource_refs, i);
    if (r->b) {
      iwhmap_remove(_map_resource_member, (void*) (uintptr_t) r->b->id);
    }
  }
  for (int i = (int) iwulist_length(&member->resource_refs) - 1; i >= 0; --i) {
    struct rct_resource_ref *r = iwulist_at2(&member->resource_refs, i);
    if (r->b) {
      if (clist && (r->b->type & RCT_TYPE_TRANSPORT_ALL)) {
        iwulist_push(clist, &r->b->id); // Collect transports to close
      }
      rct_resource_ref_lk(r->b, -1, __func__); // Release own ref
    }
  }
  if (clist && iwtp_schedule(g_env.tp, _member_close_transports_task, clist)) {
    iwulist_destroy(&clist);
  }
  iwulist_destroy_keep(&member->resource_refs);
  if (room) {
    rct_resource_ref_lk(room, -1, __func__); // Unref parent room
  }
  if (member->name) {
    free(member->name);
    member->name = 0;
  }
}

static void _room_close_if_no_members_task(void *arg) {
  wrc_resource_t room_id = (uintptr_t) arg;
  rct_room_t *room = rct_resource_by_id_locked(room_id, RCT_TYPE_ROOM, __func__);
  wrc_resource_t router_id = 0;
  if (room) {
    if (room->members == 0) {
      router_id = room->router->id;
    } else {
      room->close_pending_task = false;
    }
  }
  rct_resource_unlock(room, __func__);
  if (router_id) {
    iwrc rc = rct_router_close(router_id);
    if (rc) {
      iwlog_ecode_error2(rc, __func__);
    }
  }
}

static void _member_close_lk(void *m) {
  assert(m);
  JBL jbl, jbl2;
  rct_room_member_t *member = m;
  rct_room_t *room = member->room;

  // Notify all
  if (!jbl_create_empty_object(&jbl)) {
    jbl_set_string(jbl, "event", "ROOM_MEMBER_LEFT");
    jbl_set_string(jbl, "room", room->uuid);
    jbl_set_string(jbl, "member", member->uuid);

    jbl_clone(jbl, &jbl2);
    jbl_set_string(jbl2, "member_name", member->name);
    jbl_set_int64(jbl2, "user_id", member->user_id); // Dow not show user_id for other members
    wrc_notify_event_handlers(WRC_EVT_ROOM_MEMBER_LEFT, member->id, jbl2);

    if ((member->user_id != room->owner_user_id) && (room->flags & RCT_ROOM_LIGHT)) { // Notify only owner
      wrc_resource_t owner_id = 0;
      for (rct_room_member_t *m = room->members; m; m = m->next) {
        if (m->user_id == room->owner_user_id) {
          owner_id = m->id;
          break;
        }
      }
      if (_send_to_member(owner_id, jbl, __func__)) {
        jbl_destroy(&jbl);
      }
    } else if (_send_to_members(room->id, 0, jbl, __func__)) {
      jbl_destroy(&jbl);
    }
  }

  // Unset wss
  struct ws_session *wss = member->special;
  if (wss) {
    _wss_member_unset(wss);
  }

  // Unregister room member
  for (rct_room_member_t *p = room->members, *pp = 0; p; p = p->next) {
    if (p->id == member->id) {
      if (pp) {
        pp->next = p->next;
      } else {
        room->members = p->next;
      }
      break;
    } else {
      pp = p;
    }
  }

  if ((room->members == 0) && !room->close_pending_task) {
    room->close_pending_task = true;
    iwlog_debug("No members in %s room may be disposed in %d seconds", room->uuid, g_env.room.idle_timeout_sec);
    iwrc rc = iwn_schedule(&(struct iwn_scheduler_spec) {
      .poller = g_env.poller,
      .timeout_ms = 1000U * g_env.room.idle_timeout_sec,
      .task_fn = _room_close_if_no_members_task,
      .user_data = (void*) (uintptr_t) room->id
    });
    if (rc) {
      room->close_pending_task = false;
      iwlog_ecode_error2(rc, __func__);
    }
  }
}

static iwrc _room_leave_wss(struct ws_session *wss) {
  wrc_resource_t member_id = _wss_member_get(wss);
  if (member_id) {
    rct_resource_close(member_id);
  }
  return 0;
}

static void* _rct_member_findref_by_flag_lk(rct_room_member_t *member, uint32_t flags) {
  if (member) {
    for (int i = 0, l = iwulist_length(&member->resource_refs); i < l; ++i) {
      struct rct_resource_ref *r = iwulist_at2(&member->resource_refs, i);
      if (r->flags & flags) {
        return r->b;
      }
    }
  }
  return 0;
}

static void* _rct_member_findref_by_uuid_lk(rct_room_member_t *member, const char *uuid) {
  if (member) {
    for (int i = 0, l = iwulist_length(&member->resource_refs); i < l; ++i) {
      struct rct_resource_ref *r = iwulist_at2(&member->resource_refs, i);
      if (!strcmp(r->b->uuid, uuid)) {
        return r->b;
      }
    }
  }
  return 0;
}

static void* _rct_member_findref_by_id_lk(rct_room_member_t *member, wrc_resource_t id) {
  if (member) {
    for (int i = 0, l = iwulist_length(&member->resource_refs); i < l; ++i) {
      struct rct_resource_ref *r = iwulist_at2(&member->resource_refs, i);
      if (r->b->id == id) {
        return r->b;
      }
    }
  }
  return 0;
}

static iwrc _rct_member_uuid_fill(const char *room_uuid, int64_t user_id, char uuid_out[IW_UUID_STR_LEN]) {
  iwrc rc = 0;
  JQL q = 0;
  EJDB_DOC doc;

  if (!room_uuid || !user_id) {
    iwu_uuid4_fill(uuid_out);
    return 0;
  }

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?]"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room_uuid));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 1, pool));

  if (doc) {
    JBL_NODE n;
    RCC(rc, finish, jbl_to_node(doc->raw, &n, false, pool));
    jbn_at(n, "/events", &n);
    if (!n || (n->type != JBV_ARRAY)) {
      goto fallback;
    }
    for (JBL_NODE e = n->child; e; e = e->next) {
      if (  (e->type != JBV_ARRAY) || (e->child->type != JBV_STR)
         || (strcmp(e->child->vptr, "joined") != 0) || (jbn_length(e) < 4)) {
        continue;
      }
      JBL_NODE node_id = e->child->next->next;
      JBL_NODE node_uuid = e->child->next->next->next;
      if ((node_id->type != JBV_I64) || (node_uuid->type != JBV_STR) || (node_uuid->vsize != IW_UUID_STR_LEN)) {
        iwlog_warn("Invalid room join event data slot! Room: %s", room_uuid);
        goto fallback;
      }
      if (node_id->vi64 == user_id) {
        memcpy(uuid_out, node_uuid->vptr, IW_UUID_STR_LEN);
        goto finish;
      }
    }
  }

fallback:
  iwu_uuid4_fill(uuid_out);

finish:
  jql_destroy(&q);
  iwpool_destroy(pool);
  return rc;
}

static iwrc _rct_room_join_lk(
  const struct rct_room_join_spec *spec, wrc_resource_t room_id,
  wrc_resource_t *member_id
  ) {
  if (!spec || !member_id || !spec->wss) {
    return IW_ERROR_INVALID_ARGS;
  }
  *member_id = 0;

  iwrc rc = 0;
  rct_room_t *room = 0;
  rct_room_member_t *member = 0;
  IWPOOL *pool = 0;

  wrc_resource_t id = _wss_member_get(spec->wss);
  if (id) {
    iwlog_warn("Member %" PRIu32 " is joined already, unlinking...", id);
    _wss_member_unset(spec->wss); // Unset link to wss
    member = rct_resource_by_id_unsafe(id, RCT_TYPE_ROOM_MEMBER);
    if (member) {
      rct_resource_close_lk(member);
    }
  }

  RCB(finish,  pool = iwpool_create_empty());
  RCB(finish, member = iwpool_calloc(sizeof(*member), pool));

  member->pool = pool;
  member->type = RCT_TYPE_ROOM_MEMBER;
  member->user_id = grh_auth_get_userid(spec->wss->ws->req);
  member->wsid = spec->wss->wsid;
  member->close = _member_close_lk;
  member->dispose = _member_dispose_lk;

  if (spec->member_uuid[0]) {
    memcpy(member->uuid, spec->member_uuid, IW_UUID_STR_LEN);
  } else {
    iwu_uuid4_fill(member->uuid);
  }

  RCB(finish, member->name = strdup(spec->name));
  member->flags = spec->flags;
  member->special = spec->wss;
  iwulist_init(&member->resource_refs, 64, sizeof(struct rct_resource_ref));

  room = rct_resource_by_id_locked_lk(room_id, RCT_TYPE_ROOM, __func__);
  RCIF(!room, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  if (!room->owner_user_id) {
    room->owner_user_id = member->user_id;
  }

  rct_resource_ref_lk(member, RCT_INIT_REFS, __func__);
  member->room = rct_resource_ref_lk(room, 1, __func__);

  // Check if member is already in the room
  for (rct_room_member_t *m = room->members; m; m = m->next) {
    if (m->user_id == member->user_id) {
      rct_resource_close_lk(m);
      break;
    }
  }

  member->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(member));
  RCC(rc, finish, _wss_member_set(spec->wss, member->id));

  rct_room_member_t *m = room->members;
  if (m) {
    while (m->next) {
      m = m->next;
    }
    m->next = member;
  } else {
    room->members = member;
  }

  *member_id = member->id;

finish:
  rct_resource_ref_lk(member, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_lk(room, -1, __func__);
  if (!member) {
    iwpool_destroy(pool);
  }
  return rc;
}

/// Attach audio-level observer to the room
static iwrc _rct_room_alo_attach(wrc_resource_t room_id) {
  wrc_resource_t router_id, observer_id;
  rct_room_t *room = rct_resource_by_id_locked(room_id, RCT_TYPE_ROOM, __func__);
  if (!room) {
    rct_resource_unlock(room, __func__);
    return GR_ERROR_RESOURCE_NOT_FOUND;
  }
  router_id = room->router->id;
  rct_resource_unlock(room, __func__);

  return rct_observer_al_create(router_id,
                                g_env.alo.max_entries,
                                g_env.alo.threshold,
                                g_env.alo.interval_ms,
                                &observer_id);
}

#if (ENABLE_WHITEBOARD == 1)

static iwrc _rct_init_whiteboard(rct_room_t *room) {
  iwrc rc = 0;
  IWXSTR *link = 0;
  RCA((link = iwxstr_new()), finish);
  RCC(rc, finish, iwxstr_printf(link, "/whiteboard/#%s", room->cid));
  room->whiteboard_link = iwxstr_ptr(link);
  room->num_whiteboard_clicks = 0;

finish:
  iwxstr_destroy_keep_ptr(link);
  return rc;
}

#endif

static iwrc _rct_room_create(
  const struct rct_room_spec *spec,
  wrc_resource_t              router_id,
  wrc_resource_t             *room_id_out,
  wrc_resource_t             *member_id_out
  ) {
  if (!spec || !room_id_out || !spec->wss) {
    return IW_ERROR_INVALID_ARGS;
  }
  *room_id_out = 0;
  *member_id_out = 0;

  iwrc rc = 0;
  bool locked = false;
  wrc_resource_t member_id;
  rct_router_t *router = 0;
  rct_room_t *room = 0;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCB(finish, room = iwpool_calloc(sizeof(*room), pool));
  room->pool = pool;
  room->type = RCT_TYPE_ROOM;
  room->flags = spec->flags;
  room->close = _rct_room_close_lk;
  room->dispose = _rct_room_dispose_lk;

  iwu_uuid4_fill(room->cid);
  RCC(rc, finish, iwp_current_time_ms(&room->cid_ts, false));

#if (ENABLE_WHITEBOARD == 1)
  RCC(rc, finish, _rct_init_whiteboard(room));
#endif

  if (spec->uuid) {
    memcpy(room->uuid, spec->uuid, strlen(spec->uuid));
  } else {
    iwu_uuid4_fill(room->uuid);
  }

  RCB(finish, room->name = strdup(spec->name));
  router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__), locked = true;
  RCIF(!router, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  room->router = rct_resource_ref_lk(router, 1, __func__);
  if (router->room) {
    rct_resource_close_lk((void*) router->room);
  }
  router->room = rct_resource_ref_lk(room, RCT_INIT_REFS, __func__);

  room->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(room));

  // Join the first participant
  RCC(rc, finish, _rct_room_join_lk(&spec->member, room->id, &member_id));

  // Now save result and fire action events
  *room_id_out = room->id;
  *member_id_out = member_id;

  wrc_notify_event_handlers(WRC_EVT_ROOM_CREATED, room->id, 0);
  wrc_notify_event_handlers(WRC_EVT_ROOM_MEMBER_JOIN, member_id, 0);

finish:
  rct_resource_ref_keep_locking(room, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(router, locked, -1, __func__);
  if (!room) {
    iwpool_destroy(pool);
  }
  return rc;
}

static iwrc _member_fill_join_spec(
  const char                *room_uuid,
  struct ws_session         *wss,
  JBL_NODE                   json,
  struct rct_room_join_spec *spec
  ) {
  /* Member
     {
     "name": string,
     }
   */
  JBL_NODE n;
  iwrc rc = jbn_at(json, "/name", &n);
  if (rc || (n->type != JBV_STR)) {
    return GR_ERROR_COMMAND_INVALID_INPUT;
  }
  int64_t user_id = grh_auth_get_userid(wss->ws->req);
  spec->wss = wss;
  spec->name = n->vptr;
  return _rct_member_uuid_fill(room_uuid, user_id, spec->member_uuid);
}

static iwrc _room_fill_members_array_lk(JBL_NODE members_array, rct_room_member_t *member, IWPOOL *pool) {
  if (!member || (members_array->type != JBV_ARRAY)) {
    return IW_ERROR_INVALID_ARGS;
  }
  iwrc rc = 0;
  JBL_NODE n;
  int64_t owner_id = member->room->owner_user_id;

  for (rct_room_member_t *m = member->room->members; m; m = m->next) {
    if (m->id != member->id) {
      if ((member->room->flags & RCT_ROOM_LIGHT) && (m->user_id != owner_id) && (member->user_id != owner_id)) {
        continue;
      }
      RCC(rc, finish, jbn_from_json("[]", &n, pool));
      RCC(rc, finish, jbn_add_item_str(n, 0, m->uuid, IW_UUID_STR_LEN, 0, pool));
      RCC(rc, finish, jbn_add_item_str(n, 0, m->name, -1, 0, pool));
      RCC(rc, finish, jbn_add_item_bool(n, 0, m->user_id == member->room->owner_user_id, 0, pool));
      jbn_add_item(members_array, n);
    }
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc;
}

static iwrc _room_join(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
      room: room uuid,
      name: member name
     }
     Response: {
       room: room uuid,
       name: room name,
       member: room member uuid,
       members: [
         [member_uuid, member_name],
         ...
      ]
     }
   */
  iwrc rc = 0;
  bool locked = false;
  wrc_resource_t member_id = 0;
  struct ws_session *wss = ctx->wss;

  struct rct_room_join_spec spec = {
    .wss = ctx->wss
  };
  const char *room_uuid = 0;
  JBL_NODE data = ctx->payload, caps, n;
  if (!data) {
    rc = GR_ERROR_COMMAND_INVALID_INPUT;
    goto finish;
  }

  RCC(rc, finish, jbn_at(data, "/room", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  room_uuid = n->vptr;

  RCC(rc, finish, _member_fill_join_spec(room_uuid, wss, data, &spec));

  rct_room_t *room = rct_resource_by_uuid_locked(room_uuid, RCT_TYPE_ROOM, __func__);
  locked = true;
  if (!room) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    goto finish;
  }

  RCC(rc, finish, _rct_room_join_lk(&spec, room->id, &member_id));
  RCC(rc, finish, jbn_clone(room->router->rtp_capabilities, &caps, ctx->pool));
  caps->key = "rtpCapabilities";
  caps->klidx = (int) strlen(caps->key);

  wrc_notify_event_handlers(WRC_EVT_ROOM_MEMBER_JOIN, member_id, 0);

finish:
  if (locked) {
    rct_resource_unlock(room, __func__);
  }
  if (rc) {
    const char *error = "error.unspecified";
    if (rc == GR_ERROR_RESOURCE_NOT_FOUND) {
      error = "error.room_not_found";
    }
    return grh_ws_send_confirm(ctx, error);
  } else {
    RCC(rc, fatal, jbn_from_json("{}", &n, ctx->pool));
    rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
    if (member) {
      jbn_add_item_str(n, "room", member->room->uuid, -1, 0, ctx->pool);
      jbn_add_item_str(n, "cid", member->room->cid, -1, 0, ctx->pool);
      jbn_add_item_i64(n, "ts", member->room->cid_ts, 0, ctx->pool);
      jbn_add_item_str(n, "name", member->room->name, -1, 0, ctx->pool);
      jbn_add_item_str(n, "member", member->uuid, -1, 0, ctx->pool);
      jbn_add_item_bool(n, "owner", member->user_id == member->room->owner_user_id, 0, ctx->pool);
      jbn_add_item_bool(n, "recording", member->room->has_started_recording, 0, ctx->pool);
#if (ENABLE_WHITEBOARD == 1)
      jbn_add_item_str(n, "whiteboard", member->room->whiteboard_link, -1, 0, ctx->pool);
#endif
      jbn_add_item(n, caps);
      if (!jbn_add_item_arr(n, "members", &data, ctx->pool)) {
        _room_fill_members_array_lk(data, member, ctx->pool);
      }
    }
    rct_resource_unlock(member, __func__);
    RCGO(rc, fatal);
    return grh_ws_send_confirm2(ctx, n, 0);
  }

fatal:
  return rc;
}

static iwrc _room_create(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
     "room": {
       "uuid"?: string, // Existing room uuid (optional)
       "name": string,
       "type": webinar | meeting
     },
     "member": {
       "name": member name
     }
     }
     Response: {
      room: room uuid,
      name: room name,
      rtpCapabilities, {...},
      member: room member uuid,
      owner: boolean, // If user is a room owner
      recording: boolean,
      flags: number,
      members: [
        [member_uuid, member_name, is_owner],
        ...
      ]
     }
   */
  struct ws_session *wss = ctx->wss;
  iwrc rc = RCR(grh_wss_add_event_listener(wss, _wss_closed_listener, 0));

  JBL_NODE room_node, member_node, caps, n;
  const char *uuid = 0, *error = 0;

  wrc_resource_t router_id = 0,
                 room_id = 0,
                 member_id = 0;

  if (!ctx->payload) {
    rc = GR_ERROR_COMMAND_INVALID_INPUT;
    goto finish;
  }

  RCC(rc, finish, jbn_at(ctx->payload, "/room", &room_node));
  RCC(rc, finish, jbn_at(ctx->payload, "/member", &member_node));

  jbn_at(room_node, "/uuid", &n); // Check if uuid was given
  if (n && (n->type == JBV_STR)) {
    uuid = n->vptr;
  }

  if (uuid) {
    const char *uuid = n->vptr;
    rct_room_t *room = rct_resource_by_uuid_locked(uuid, RCT_TYPE_ROOM, __func__);
    rct_resource_unlock_keep_ref(room); // Unlock  +1 ref on room
    if (room && !room->close_pending) { // An active room was found, try to join it
      JBL_NODE spec;
      RCC(rc, finish, jbn_at(member_node, "/name", &n));
      CHECK_JBN_TYPE(n, finish, JBV_STR);
      RCC(rc, finish, jbn_from_json("{}", &spec, ctx->pool));
      RCC(rc, finish, jbn_add_item_str(spec, "room", uuid, -1, 0, ctx->pool));
      RCC(rc, finish, jbn_add_item_str(spec, "name", n->vptr, -1, 0, ctx->pool));
      ctx->payload = spec; // Overwrite payload request
      rc = _room_join(ctx, op);
      rct_resource_ref_keep_locking(room, false, -1, __func__);
      return rc;
    } else {
      //if (!gr_au)
      // todo: check user roles
    }
  }

  gr_lic_error(&error);
  if (error) {
    rc = GR_ERROR_LICENSE;
    goto finish;
  }

  struct rct_room_spec spec = {
    .uuid = uuid,
    .wss  = ctx->wss,
  };

  RCC(rc, finish, jbn_at(room_node, "/name", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  spec.name = n->vptr;

  RCC(rc, finish, jbn_at(room_node, "/type", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);

  if (!strcmp(n->vptr, "webinar")) {
    spec.flags |= RCT_ROOM_WEBINAR;
  } else {
    spec.flags |= RCT_ROOM_MEETING;
    if (!g_env.alo.disabled) {
      spec.flags |= RCT_ROOM_ALO;
    }
  }

  RCC(rc, finish, _member_fill_join_spec(spec.uuid, ctx->wss, member_node, &spec.member));
  RCC(rc, finish, rct_router_create(0, 0, &router_id));
  RCC(rc, finish, _rct_room_create(&spec, router_id, &room_id, &member_id));

  rct_router_t *router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__);
  if (router) {
    RCC(rc, finish, jbn_clone(router->rtp_capabilities, &caps, ctx->pool));
    caps->key = "rtpCapabilities";
    caps->klidx = (int) strlen(caps->key);
  } else {
    rc = IW_ERROR_FAIL;
  }
  rct_resource_unlock(router, __func__);

  if (spec.flags & RCT_ROOM_ALO) {
    RCC(rc, finish, _rct_room_alo_attach(room_id));
  }

finish:
  if (rc) {
    if (!error) {
      error = "error.unspecified";
    }
    if (router_id) {
      rct_router_close(router_id);
    }
    if (rc == GR_ERROR_COMMAND_INVALID_INPUT) {
      error = "error.invalid_input";
    } else {
      iwlog_ecode_error3(rc);
    }
    return grh_ws_send_confirm(ctx, error);
  } else {
    RCC(rc, fatal, jbn_from_json("{}", &n, ctx->pool));
    rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
    if (member) {
      jbn_add_item_str(n, "room", member->room->uuid, -1, 0, ctx->pool);
      jbn_add_item_str(n, "cid", member->room->cid, -1, 0, ctx->pool);
      jbn_add_item_i64(n, "ts", member->room->cid_ts, 0, ctx->pool);
      jbn_add_item_str(n, "name", member->room->name, -1, 0, ctx->pool);
      jbn_add_item_i64(n, "flags", member->room->flags, 0, ctx->pool);
      jbn_add_item_str(n, "member", member->uuid, -1, 0, ctx->pool);
      jbn_add_item_bool(n, "owner", member->user_id == member->room->owner_user_id, 0, ctx->pool);
      jbn_add_item_bool(n, "recording", member->room->has_started_recording, 0, ctx->pool);
#if (ENABLE_WHITEBOARD == 1)
      jbn_add_item_str(n, "whiteboard", member->room->whiteboard_link, -1, 0, ctx->pool);
#endif
      jbn_add_item(n, caps);
      if (!jbn_add_item_arr(n, "members", &member_node, ctx->pool)) {
        _room_fill_members_array_lk(member_node, member, ctx->pool);
      }
    }
    rct_resource_unlock(member, __func__);
    RCGO(rc, fatal);
    return grh_ws_send_confirm2(ctx, n, 0);
  }

fatal:
  return rc;
}

static void _room_send_broadcast_message(struct ws_message_ctx *ctx, const char *message, const char *tag) {
  JBL jbl = 0;
  wrc_resource_t room_id = 0;
  wrc_resource_t room_member_id = _wss_member_get(ctx->wss);
  bool locked = false;

  iwrc rc = jbl_create_empty_object(&jbl);
  RCGO(rc, finish);

  rct_room_member_t *member = rct_resource_by_id_locked(room_member_id, RCT_TYPE_ROOM_MEMBER, tag);
  locked = true;
  if (member && !(member->room->flags & RCT_ROOM_LIGHT)) {
    // No broadcast messages for webinars
    // todo: allow for webinar admin?
    room_id = member->room->id;
    RCC(rc, finish, jbl_set_string(jbl, "cmd", "broadcast_message"));
    RCC(rc, finish, jbl_set_string(jbl, "message", message));
    RCC(rc, finish, jbl_set_string(jbl, "member", member->uuid));
    RCC(rc, finish, jbl_set_string(jbl, "member_name", member->name));
  }
  rct_resource_unlock(member, tag), locked = false;

  if (room_id) {
    rc = _send_to_members(room_id, room_member_id, jbl, tag);
  }

finish:
  if (locked) {
    rct_resource_unlock(member, tag);
  }
  if (rc) {
    jbl_destroy(&jbl);
    iwlog_ecode_error3(rc);
  }
}

static iwrc _room_leave(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
      message?: string
     }
   */
  JBL_NODE n;
  grh_ws_send_confirm(ctx, 0);
  if (!jbn_at(ctx->payload, "/message", &n) && (n->type == JBV_STR) && (n->vsize > 0)) {
    _room_send_broadcast_message(ctx, n->vptr, __func__);
  }
  _room_leave_wss(ctx->wss);
  return 0;
}

static iwrc _rtp_capabilities(struct ws_message_ctx *ctx, void *op) {
  iwrc rc = 0;
  JBL_NODE n = 0;
  const char *error = 0;
  bool locked = false;
  wrc_resource_t member_id = _wss_member_get(ctx->wss);
  rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  locked = true;
  if (!member) {
    error = "error.not_a_room_member";
    goto finish;
  }
  RCC(rc, finish, jbn_clone(member->room->router->rtp_capabilities, &n, ctx->pool));
  rct_resource_unlock(member, __func__), locked = false;

finish:
  if (locked) {
    rct_resource_unlock(member, __func__);
  }
  SIMPLE_HANDLER_FINISH_RET(n);
}

static iwrc _room_info_get(struct ws_message_ctx *ctx, void *op) {
  JBL_NODE n;
  jbn_at(ctx->payload, "/uuid", &n);
  if (!n || (n->type != JBV_STR)) {
    return 0;
  }
  JQL q = 0;
  EJDB_DOC res = 0;
  iwrc rc = jql_create(&q, "rooms", "/[uuid = :?] | /{uuid,name,flags}");
  RCGO(rc, finish);
  RCC(rc, finish, jql_set_str(q, 0, 0, n->vptr));
  RCC(rc, finish, ejdb_list(g_env.db, q, &res, 1, ctx->pool));
  if (res && res->node) {
    JBL_NODE n2 = res->node;
    int64_t user_id = grh_auth_get_userid(ctx->wss->ws->req);
    rct_room_t *room = rct_resource_by_uuid_locked(n->vptr, RCT_TYPE_ROOM, __func__);
    jbn_add_item_bool(n2, "alive", room != 0, 0, ctx->pool);
    jbn_add_item_bool(n2, "owner", room != 0 && room->owner_user_id == user_id, 0, ctx->pool);
    rct_resource_unlock(room, __func__);
    rc = grh_ws_send_confirm2(ctx, n2, 0);
  } else {
    rc = grh_ws_send_confirm(ctx, 0);
  }
finish:
  jql_destroy(&q);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return 0;
}

static iwrc _room_message(struct ws_message_ctx *ctx, void *op) {
  /*
     MessageNode: string | {
       t: string, // tag name
       a: {[name]: string} // attributes
       c: [MessageNode] // children nodes
     }
     Payload: {
      message: [MessageNode],
      recipient?: recipient uuid
     }
     Response: {
      messages: ChatRawMessage[]
     }
   */

  iwrc rc = 0;
  const char *error = 0;

  JQL q = 0;
  char *message = 0;
  char *member_name = 0;
  JBL_NODE n, resp = 0, patch, patch_op, patch_value;
  wrc_resource_t room_id, recipient_id = 0, member_id = _wss_member_get(ctx->wss);
  int64_t user_id = grh_auth_get_userid(ctx->wss->ws->req);

  char room_uuid[IW_UUID_STR_LEN + 1];
  char recipient_uuid[IW_UUID_STR_LEN + 1];
  char member_uuid[IW_UUID_STR_LEN + 1];

  jbn_at(ctx->payload, "/message", &n);
  if (!n || (n->type != JBV_ARRAY)) {
    error = "error.invalid.input";
    goto finish;
  }
  message = html_safe_alloc_from_jbn(n);
  if (!message) {
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    goto finish;
  }
  if (strlen(message) == 0) {
    error = "error.invalid.input";
    goto finish;
  }
  jbn_at(ctx->payload, "/recipient", &n);
  if (n && (n->type == JBV_STR)) {
    rct_room_member_t *member = rct_resource_by_uuid_locked(n->vptr, RCT_TYPE_ROOM_MEMBER, __func__);
    if (member) {
      recipient_id = member->user_id;
      memcpy(recipient_uuid, member->uuid, IW_UUID_STR_LEN + 1);
    }
    rct_resource_unlock(member, __func__);
  }

  rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  if (!member) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    rct_resource_unlock(member, __func__);
    goto finish;
  }
  room_id = member->room->id;
  memcpy(room_uuid, member->room->uuid, IW_UUID_STR_LEN + 1);
  memcpy(member_uuid, member->uuid, IW_UUID_STR_LEN + 1);
  member_name = strdup(member->name);
  if (!member_name) {
    rc = iwrc_set_errno(IW_ERROR_ALLOC, errno);
    rct_resource_unlock(member, __func__);
    goto finish;
  }
  rct_resource_unlock(member, __func__);

  uint64_t ts;
  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  RCC(rc, finish, jbn_from_json("[]", &patch, ctx->pool));
  RCC(rc, finish, jbn_add_item_obj(patch, 0, &patch_op, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(patch_op, "op", "add", sizeof("add") - 1, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(patch_op, "path", "/events/-", sizeof("/events/-") - 1, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_arr(patch_op, "value", &patch_value, ctx->pool));

  // ["message", event_time, member_id, member_name, recipient_id, message]
  RCC(rc, finish, jbn_add_item_str(patch_value, 0, "message", sizeof("message") - 1, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_i64(patch_value, 0, ts, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_i64(patch_value, 0, user_id, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(patch_value, 0, member_name, -1, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_i64(patch_value, 0, recipient_id, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(patch_value, 0, message, -1, 0, ctx->pool));

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room_uuid));
  RCC(rc, finish, jql_set_json(q, 0, 1, patch));

  RCC(rc, finish, ejdb_update(g_env.db, q));

  RCC(rc, finish, jbn_from_json("{}", &resp, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(resp, "cmd", "message", sizeof("message") - 1, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_arr(resp, "message", &n, ctx->pool));

  // [time, memberId, memberName, recipientId | null, message]
  RCC(rc, finish, jbn_add_item_i64(n, 0, ts, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(n, 0, member_uuid, IW_UUID_STR_LEN, 0, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(n, 0, member_name, -1, 0, ctx->pool));
  if (recipient_id) {
    RCC(rc, finish, jbn_add_item_str(n, 0, recipient_uuid, IW_UUID_STR_LEN, 0, ctx->pool));
  } else {
    RCC(rc, finish, jbn_add_item_null(n, 0, ctx->pool));
  }
  RCC(rc, finish, jbn_add_item_str(n, 0, message, -1, 0, ctx->pool));

  if (recipient_id) {
    JBL jbl;
    RCC(rc, finish, jbl_from_node(&jbl, resp));
    if (_send_to_member(recipient_id, jbl, __func__)) {
      jbl_destroy(&jbl);
    }
  } else {
    JBL jbl;
    RCC(rc, finish, jbl_from_node(&jbl, resp));
    if (_send_to_members(room_id, member_id, jbl, __func__)) {
      jbl_destroy(&jbl);
    }
  }

finish:
  free(member_name);
  free(message);
  jql_destroy(&q);
  SIMPLE_HANDLER_FINISH_RET(resp);
}

static iwrc _room_messages(struct ws_message_ctx *ctx, void *op) {
  /*
     Payload: {}
     Response: {
      messages: ChatRawMessage[]
     }
   */

  iwrc rc = 0;
  const char *error = 0;

  JQL q = 0;
  EJDB_DOC doc;
  JBL_NODE n, resp = 0, messages;
  char room_uuid[IW_UUID_STR_LEN + 1];

  rct_room_t *room = 0;
  uint64_t user_id = 0;
  wrc_resource_t room_id;
  rct_room_member_t *member = rct_resource_by_id_locked(_wss_member_get(ctx->wss), RCT_TYPE_ROOM_MEMBER, __func__);
  if (!member) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    rct_resource_unlock(member, __func__);
    goto finish;
  }

  room_id = member->room->id;
  user_id = member->user_id;
  memcpy(room_uuid, member->room->uuid, IW_UUID_STR_LEN + 1);
  rct_resource_unlock(member, __func__);

  RCC(rc, finish, jbn_from_json("{}", &resp, ctx->pool));
  RCC(rc, finish, jbn_add_item_arr(resp, "messages", &messages, ctx->pool));

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?]"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room_uuid));

  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 1, ctx->pool));

  if (!doc) {
    goto finish;
  }
  RCC(rc, finish, jbl_to_node(doc->raw, &n, false, ctx->pool));
  jbn_at(n, "/events", &n);
  if (!n || (n->type != JBV_ARRAY)) {
    goto finish;
  }

  room = rct_resource_by_id_locked(room_id, RCT_TYPE_ROOM, __func__);
  if (!room) {
    rct_resource_unlock(room, __func__);
    goto finish;
  }
  for (JBL_NODE e = n->child; e; e = e->next) {
    if (  (e->type != JBV_ARRAY)
       || (e->child->type != JBV_STR)
       || (strcmp(e->child->vptr, "message") != 0)
       || (jbn_length(e) < 6)) {
      continue;
    }
    JBL_NODE message;
    JBL_NODE node_ts = e->child->next;
    JBL_NODE node_member_id = e->child->next->next;
    JBL_NODE node_member_name = e->child->next->next->next;
    JBL_NODE node_recipient_id = e->child->next->next->next->next;
    JBL_NODE node_message = e->child->next->next->next->next->next;
    if (  (node_ts->type != JBV_I64)
       || (node_member_id->type != JBV_I64)
       || (node_member_name->type != JBV_STR)
       || (node_recipient_id->type != JBV_I64)
       || (node_message->type != JBV_STR)) {
      continue;
    }
    if (!(  (node_recipient_id->vi64 == 0) || (node_recipient_id->vi64 == user_id)
         || (node_member_id->vi64 == user_id))) {
      continue;
    }
    const char *member_uuid = 0;
    const char *recipient_uuid = 0;
    for (rct_room_member_t *m = room->members; m; m = m->next) {
      if (m->user_id == node_member_id->vi64) {
        member_uuid = m->uuid;
      } else if (m->user_id == node_recipient_id->vi64) {
        recipient_uuid = m->uuid;
      }
    }
    // [time, memberId | null, memberName, recipientId | null, message]
    RCC(rc, finish, jbn_add_item_arr(messages, 0, &message, ctx->pool));
    RCC(rc, finish, jbn_add_item_i64(message, 0, node_ts->vi64, 0, ctx->pool));
    if (member_uuid) {
      RCC(rc, finish, jbn_add_item_str(message, 0, member_uuid, IW_UUID_STR_LEN, 0, ctx->pool));
    } else {
      RCC(rc, finish, jbn_add_item_null(message, 0, ctx->pool));
    }
    RCC(rc, finish, jbn_add_item_str(message, 0, node_member_name->vptr, node_member_name->vsize, 0, ctx->pool));
    if (recipient_uuid) {
      RCC(rc, finish, jbn_add_item_str(message, 0, recipient_uuid, IW_UUID_STR_LEN, 0, ctx->pool));
    } else {
      RCC(rc, finish, jbn_add_item_null(message, 0, ctx->pool));
    }
    RCC(rc, finish, jbn_add_item_str(message, 0, node_message->vptr, node_message->vsize, 0, ctx->pool));
  }

finish:
  if (room) {
    rct_resource_unlock(room, __func__);
  }
  jql_destroy(&q);
  SIMPLE_HANDLER_FINISH_RET(resp);
}

static iwrc _room_backup_previous(wrc_resource_t room_id) {
  int64_t id;
  rct_resource_base_t b;
  JQL q = 0;
  EJDB_LIST res = 0;

  char uuid[IW_UUID_STR_LEN + 1];
  uuid[IW_UUID_STR_LEN] = '\0';

  iwrc rc = rct_resource_probe_by_id(room_id, &b);
  if (rc) {
    if (rc == IW_ERROR_NOT_EXISTS) {
      return 0;
    }
    return rc;
  }
  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | /*"));
  RCC(rc, finish, jql_set_str(q, 0, 0, b.uuid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &res));
  if (res->first) {
    // Swap /uuid and /cid
    JBL_NODE doc = res->first->node, n_cid, n_uuid, n_sess;
    assert(doc);
    rc = jbn_at(doc, "/cid", &n_cid);
    if (rc || n_cid->type != JBV_STR || n_cid->vsize != IW_UUID_STR_LEN) { // Perhaps a legacy data
      rc = 0;
      goto finish;
    }
    memcpy(uuid, n_cid->vptr, IW_UUID_STR_LEN);
    rc = jbn_at(doc, "/uuid", &n_uuid);
    if (rc || n_uuid->type != JBV_STR || n_uuid->vsize != IW_UUID_STR_LEN) { // Perhaps a legacy data
      rc = 0;
      goto finish;
    }
    rc = jbn_at(doc, "/session", &n_sess);
    if (rc || n_sess->type != JBV_BOOL) { // Perhaps a legacy data
      rc = 0;
      goto finish;
    }
    n_sess->vbool = true;
    memcpy((char*) n_cid->vptr, n_uuid->vptr, IW_UUID_STR_LEN);
    memcpy((char*) n_uuid->vptr, uuid, IW_UUID_STR_LEN);
    RCC(rc, finish, ejdb_put_new_jbn(g_env.db, "rooms", doc, &id));
  }

finish:
  jql_destroy(&q);
  ejdb_list_destroy(&res);
  return rc;
}

static void _on_room_created(wrc_resource_t room_id) {
  JBL_NODE n, n2, n3;
  iwrc rc = 0;
  JQL q = 0;
  IWPOOL *pool = 0;
  bool locked = false;

  RCC(rc, finish, _room_backup_previous(room_id));
  RCB(finish, pool = iwpool_create_empty());

  rct_room_t *room = rct_resource_by_id_locked(room_id, RCT_TYPE_ROOM, __func__);
  locked = true;
  if (!room) {
    goto finish;
  }
  RCC(rc, finish, jbn_from_json("{}", &n, pool));
  RCC(rc, finish, jbn_add_item_str(n, "uuid", room->uuid, IW_UUID_STR_LEN, 0, pool));
  RCC(rc, finish, jbn_add_item_str(n, "cid", room->cid, IW_UUID_STR_LEN, 0, pool));
  RCC(rc, finish, jbn_add_item_bool(n, "session", false, 0, pool));
  RCC(rc, finish, jbn_add_item_str(n, "name", room->name, -1, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "flags", room->flags, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "owner", room->owner_user_id, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "ctime", room->cid_ts, 0, pool));
  RCC(rc, finish, jbn_add_item_null(n, "recf", pool));
  RCC(rc, finish, jbn_add_item_arr(n, "events", &n2, pool));

  RCC(rc, finish, jbn_add_item_arr(n2, 0, &n3, pool));
  RCC(rc, finish, jbn_add_item_str(n3, 0, "created", -1, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n3, 0, room->cid_ts, 0, pool));


  rct_resource_unlock(room, __func__), locked = false;

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | upsert :?"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room->uuid));
  RCC(rc, finish, jql_set_json(q, 0, 1, n));
  RCC(rc, finish, ejdb_update(g_env.db, q));

finish:
  if (locked) {
    rct_resource_unlock(room, __func__);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _on_room_closed(JBL event_data) {
  JBL uuid;
  JBL_NODE n;
  uint64_t ts;
  JQL q = 0;
  iwrc rc = 0;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCC(rc, finish, jbl_at(event_data, "/room", &uuid));
  if (jbl_type(uuid) != JBV_STR) {
    goto finish;
  }
  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  RCC(rc, finish, jbn_from_json_printf(
        &n, pool,
        "["
        "{\"op\":\"add\", \"path\":\"/events/-\", "
        "\"value\":[\"closed\", %" PRIu64 "]}"
        "]",
        ts
        ));

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
  RCC(rc, finish, jql_set_str(q, 0, 0, jbl_get_str(uuid)));
  RCC(rc, finish, jql_set_json(q, 0, 1, n));

  RCC(rc, finish, ejdb_update(g_env.db, q));

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  jbl_destroy(&uuid);
  iwpool_destroy(pool);
}

static void _on_member_join(wrc_resource_t member_id) {
  uint64_t ts;
  int64_t user_id, room_id;

  iwrc rc = 0;
  JQL q = 0;
  JBL jbl = 0;
  JBL_NODE n = 0;
  bool locked = false;
  bool owner = false;
  wrc_resource_t owner_member_id = 0;
  int room_flags;

  char *member_name = 0;
  char room_uuid[IW_UUID_STR_LEN + 1];
  char room_cid[IW_UUID_STR_LEN + 1];
  char member_uuid[IW_UUID_STR_LEN + 1];

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);
  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  locked = true;
  if (!member) {
    goto finish;
  }
  room_id = member->room->id;
  user_id = member->user_id;

  memcpy(room_uuid, member->room->uuid, sizeof(room_uuid));
  memcpy(room_cid, member->room->cid, sizeof(room_cid));
  memcpy(member_uuid, member->uuid, sizeof(member_uuid));
  room_flags = member->room->flags;
  member_name = strdup(member->name);
  owner = member->room->owner_user_id == member->user_id;

  if (!owner) {
    for (rct_room_member_t *m = member->room->members; m; m = m->next) {
      if (m->user_id == member->room->owner_user_id) {
        owner_member_id = m->id;
        break;
      }
    }
  }
  rct_resource_unlock(member, __func__), locked = false;

  if (owner || !(room_flags & RCT_ROOM_LIGHT)) {
    JBL_NODE n_name;
    RCC(rc, finish, jbn_from_json_printf(
          &n, pool,
          "["
          "{\"op\":\"add\", \"path\":\"/events/-\", "
          "\"value\":[\"joined\", %" PRIu64 ", %" PRId64 ", \"%s\", \"\"]}"
          "]",
          ts, user_id, member_uuid));
    RCC(rc, finish, jbn_at(n, "/0/value/4", &n_name));
    if (n_name && n_name->type == JBV_STR) {
      n_name->vptr = member_name;
      n_name->vsize = (int) strlen(member_name);
    }
    RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
    RCC(rc, finish, jql_set_str(q, 0, 0, room_uuid));
    RCC(rc, finish, jql_set_json(q, 0, 1, n));
    RCC(rc, finish, ejdb_update(g_env.db, q));
  }

  // Register room participation
  {
    int64_t id;
    char key[sizeof(room_cid) + NUMBUSZ + 1]; // Buffer for @joins/k
    snprintf(key, sizeof(key), "%" PRId64 ":%s", user_id, room_cid);
    RCC(rc, finish, jbl_create_empty_object(&jbl));
    jbl_set_string(jbl, "k", key);
    jbl_set_string(jbl, "u", room_uuid);
    jbl_set_int64(jbl, "t", ts);
    jbl_set_bool(jbl, "o", owner);
    rc = ejdb_put_new(g_env.db, "joins", jbl, &id);
    if (rc == EJDB_ERROR_UNIQUE_INDEX_CONSTRAINT_VIOLATED) {
      rc = 0;
    }
    jbl_destroy(&jbl);
    RCGO(rc, finish);
  }

  // Send remote ROOM_MEMBER_JOIN event
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  jbl_set_string(jbl, "event", "ROOM_MEMBER_JOIN");
  jbl_set_string(jbl, "room", room_uuid);
  jbl_set_string(jbl, "member", member_uuid);
  jbl_set_string(jbl, "name", member_name);
  jbl_set_bool(jbl, "owner", owner);

  if (!owner && (room_flags & RCT_ROOM_LIGHT)) { // Notify only room owner
    if (_send_to_member(owner_member_id, jbl, __func__)) {
      jbl_destroy(&jbl);
    }
  } else if (_send_to_members(room_id, member_id, jbl, __func__)) {
    jbl_destroy(&jbl);
  }

finish:
  if (locked) {
    rct_resource_unlock(member, __func__);
  }
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  free(member_name);
  jql_destroy(&q);
  iwpool_destroy(pool);
}

static void _on_member_left(JBL event_data) {
  assert(event_data);

  uint64_t ts;
  JBL_NODE n_data, n, n_member_name, n_apply;
  int64_t user_id, owner_user_id = 0;
  rct_room_t *room;
  const char *uuid;

  JQL q = 0;
  iwrc rc = 0;
  int room_flags = 0;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);
  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  RCC(rc, finish, jbl_to_node(event_data, &n_data, false, pool));
  RCC(rc, finish, jbn_at(n_data, "/room", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);

  uuid = n->vptr;

  room = rct_resource_by_uuid_locked(uuid, RCT_TYPE_ROOM, __func__);
  if (room) {
    room_flags = room->flags;
    owner_user_id = room->owner_user_id;
  }
  rct_resource_unlock(room, __func__);

  RCC(rc, finish, jbn_at(n_data, "/user_id", &n));
  if (n->type != JBV_I64) {
    goto finish;
  }
  RCC(rc, finish, jbn_at(n_data, "/member_name", &n_member_name));
  if (n_member_name->type != JBV_STR) {
    goto finish;
  }

  user_id = n->vi64;

  if ((user_id == owner_user_id) || !(room_flags & RCT_ROOM_LIGHT)) {
    JBL_NODE n2;
    RCC(rc, finish, jbn_from_json_printf(
          &n_apply, pool,
          "["
          "{\"op\":\"add\", \"path\":\"/events/-\", "
          "\"value\":[\"left\", %" PRIu64 ", %" PRId64 ", \"\"]}"
          "]",
          ts, user_id));
    RCC(rc, finish, jbn_at(n_apply, "/0/value/3", &n2));
    if (n2 && n2->type == JBV_STR) {
      n2->vptr = n_member_name->vptr;
      n2->vsize = n_member_name->vsize;
    }
    RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
    RCC(rc, finish, jql_set_str(q, 0, 0, uuid));
    RCC(rc, finish, jql_set_json(q, 0, 1, n_apply));
    rc = ejdb_update(g_env.db, q);
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
}

static iwrc _transports_init(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
      rtpCapabilities: {...},
      // Optional direction mask
      direction?: MRES_RECV_TRANSPORT | MRES_RECV_TRANSPORT
     }
     Response: {
      recvTransport: {
            id: transport uuid,
            iceParameters  : transport.iceParameters,
            iceCandidates  : transport.iceCandidates,
            dtlsParameters : transport.dtlsParameters,
            sctpParameters : transport.sctpParameters
      },
      sendTransport: {
          ...
      }
     }
   */
  iwrc rc = 0;
  const char *error = 0;

  rct_transport_webrtc_t *transport;
  rct_room_member_t *member = 0;

  uint32_t direction = 0;
  bool locked = false, need_sts = false, need_rts = false;
  rct_transport_webrtc_spec_t *rts = 0, *sts = 0;
  wrc_resource_t router_id, tsids[2] = { 0 }, cids[2] = { 0 };

  JBL_NODE n;
  RCC(rc, finish, jbn_from_json("{}", &n, ctx->pool));

  {
    JBL_NODE n;
    if ((jbn_at(ctx->payload, "/direction", &n) == 0) && (n->type == JBV_I64)) {
      direction = n->vi64 & (MRES_SEND_TRANSPORT | MRES_RECV_TRANSPORT);
    }
  }

  member = rct_resource_by_id_locked(_wss_member_get(ctx->wss), RCT_TYPE_ROOM_MEMBER, __func__);
  locked = true;
  if (!member) {
    error = "error.not_a_room_member";
    goto finish;
  }
  if (!member->rtp_capabilities) { // Client device rtpCapabilities
    JBL_NODE n;
    RCC(rc, finish, jbn_at(ctx->payload, "/rtpCapabilities", &n));
    RCC(rc, finish, jbn_clone(n, &member->rtp_capabilities, member->pool));
  }

  router_id = member->room->router->id;

  if (!direction || (direction & MRES_RECV_TRANSPORT)) {
    need_rts = true;
  }
  if (!direction || (direction & MRES_SEND_TRANSPORT)) {
    need_sts = (member->room->flags & RCT_ROOM_MEETING)
               || member->room->owner_user_id == member->user_id;
  }

  if (need_rts) {
    rct_resource_base_t *b = _rct_member_findref_by_flag_lk(member, MRES_RECV_TRANSPORT);
    if (b) {
      cids[0] = b->id;
    }
  }
  if (need_sts) {
    rct_resource_base_t *b = _rct_member_findref_by_flag_lk(member, MRES_SEND_TRANSPORT);
    if (b) {
      cids[1] = b->id;
    }
  }
  rct_unlock(), locked = false; // Unlock but keeping +1 ref to member

  for (int i = 0; i < 2; ++i) {
    if (cids[i]) {
      rct_transport_close(cids[i]);
    }
  }

  if (need_rts) {
    RCC(rc, finish, rct_transport_webrtc_spec_create(RCT_WEBRTC_DEFAULT_FLAGS, &rts));
    RCC(rc, finish, rct_transport_webrtc_create(rts, router_id, &tsids[0]));
  }
  if (need_sts) {
    RCC(rc, finish, rct_transport_webrtc_spec_create(RCT_WEBRTC_DEFAULT_FLAGS, &sts));
    RCC(rc, finish, rct_transport_webrtc_create(sts, router_id, &tsids[1]));
  }

  rct_lock(), locked = true; // Lock again
  for (int i = 0; i < 2; ++i) {
    uint32_t flags = i == 0 ? MRES_RECV_TRANSPORT : MRES_SEND_TRANSPORT;
    if (tsids[i] && (transport = rct_resource_by_id_unsafe(tsids[i], RCT_TYPE_TRANSPORT_WEBRTC))) {
      JBL_NODE spec, data;
      RCC(rc, finish, jbn_at(transport->data, "/data", &data));
      RCC(rc, finish, jbn_from_json("{}", &spec, ctx->pool));
      RCC(rc, finish, jbn_add_item_str(spec, "id", transport->uuid, -1, 0, ctx->pool));
      RCC(rc, finish, jbn_copy_paths(data, spec, (const char*[]) {
        "/iceParameters",
        "/iceCandidates",
        "/dtlsParameters",
        "/sctpParameters",
        0
      }, true, false, ctx->pool));
      if (flags & MRES_RECV_TRANSPORT) {
        spec->key = "recvTransport";
      } else {
        spec->key = "sendTransport";
      }
      spec->klidx = (int) strlen(spec->key);
      jbn_add_item(n, spec);

      // Good, now finish registration
      RCC(rc, finish, iwulist_unshift(&member->resource_refs, &(struct rct_resource_ref) {
        .b = rct_resource_ref_lk(transport, 1, __func__),
        .flags = flags
      }));
      RCC(rc, finish, iwhmap_put(_map_resource_member,
                                 (void*) (uintptr_t) transport->id, (void*) (uintptr_t) member->id));
      // All is ok, keep transport open at exit
      tsids[i] = 0;
    }
  }

finish:
  rct_resource_ref_unlock(member, locked, -1, __func__);
  if (rc) {
    if (!error) {
      error = "error.unspecified";
    }
    iwlog_ecode_error3(rc);
    for (int i = 0; i < 2; ++i) {
      if (tsids[i]) {
        rct_transport_close(tsids[i]);
      }
    }
  }
  return grh_ws_send_confirm2(ctx, n, error);
}

static iwrc _transport_connect(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
      uuid: transport UUID
      dtlsParameters: {...}
     }
     Response: {}
   */
  iwrc rc = 0;
  const char *error = 0;

  JBL_NODE n;
  wrc_resource_t transport_id;
  rct_transport_connect_t *spec;
  rct_room_member_t *member = 0;
  const char *uuid;

  RCC(rc, finish, jbn_at(ctx->payload, "/uuid", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  uuid = n->vptr;

  RCC(rc, finish, jbn_at(ctx->payload, "/dtlsParameters", &n));
  CHECK_JBN_TYPE(n, finish, JBV_OBJECT);

  member = rct_resource_by_id_locked(_wss_member_get(ctx->wss), RCT_TYPE_ROOM_MEMBER, __func__);
  rct_transport_t *transport = _rct_member_findref_by_uuid_lk(member, uuid);
  transport_id = transport ? transport->id : 0;
  rct_resource_unlock(member, __func__);

  if (!transport_id) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    goto finish;
  }

  RCC(rc, finish, rct_transport_connect_spec_from_json(n, &spec));
  RCC(rc, finish, rct_transport_connect(transport_id, spec));

finish:
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _consumer_create(wrc_resource_t member_id, wrc_resource_t producer_id) {
  iwrc rc = 0;

  bool locked, can_consume;
  JBL jbl;
  JBL_NODE resp, n;
  wrc_resource_t consumer_transport_id, consumer_id, producer_member_id;

  rct_transport_t *transport;
  rct_producer_t *producer;
  rct_consumer_t *consumer = 0;
  rct_room_member_t *producer_member = 0;

  IWPOOL *pool = 0;

  rct_room_member_t *member = rct_resource_by_id_locked(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  locked = true;
  if (!member) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    goto finish;
  }
  producer_member_id = (uintptr_t) iwhmap_get(_map_resource_member, (void*) (uintptr_t) producer_id);
  producer_member = rct_resource_by_id_locked_lk(producer_member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  transport = _rct_member_findref_by_flag_lk(member, MRES_RECV_TRANSPORT);
  if (!producer_member || !transport) {
    iwlog_warn("No recv transport or producer member for consumer member %u", member_id);
    goto finish;
  }
  consumer_transport_id = transport->id;
  rct_unlock(), locked = false;

  RCC(rc, finish, rct_producer_can_consume2(producer_id, member->rtp_capabilities, &can_consume));
  if (!can_consume) {
    iwlog_warn("Cannot consume producer %" PRIu32 " member: %u", producer_id, member_id);
    fprintf(stderr, "Member RTP caps:\n");
    jbn_as_json(member->rtp_capabilities, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
    producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER, __func__);
    if (producer) {
      fprintf(stderr, "Producer RTP parameters:\n");
      jbn_as_json(producer->spec->rtp_parameters, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
      fprintf(stderr, "Producer consumable RTP parameters:\n");
      jbn_as_json(producer->spec->consumable_rtp_parameters, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
    }
    rct_resource_unlock(producer, __func__);
    goto finish;
  }

  RCC(rc, finish, rct_consumer_create2(consumer_transport_id, producer_id, member->rtp_capabilities, true, 0,
                                       &consumer_id));

  // Now create consumer message
  pool = iwpool_create(512);
  RCC(rc, finish, jbn_from_json("{}", &resp, pool));
  RCC(rc, finish, jbn_add_item_str(resp, "cmd", "consumer", sizeof("consumer") - 1, 0, pool));

  consumer = rct_resource_by_id_locked(consumer_id, RCT_TYPE_CONSUMER, __func__);
  locked = true;
  if (!consumer || (consumer->producer->type != RCT_TYPE_PRODUCER)) {
    goto finish;
  }
  producer = (void*) consumer->producer;

  RCC(rc, finish, jbn_add_item_str(resp, "id", consumer->uuid, IW_UUID_STR_LEN, 0, pool));
  RCC(rc, finish, jbn_add_item_str(resp, "memberId", producer_member->uuid, IW_UUID_STR_LEN, 0, pool));
  RCC(rc, finish, jbn_add_item_str(resp, "producerId", producer->uuid, IW_UUID_STR_LEN, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(resp, "kind", producer->spec->rtp_kind, 0, pool));
  RCC(rc, finish, jbn_add_item_bool(resp, "producerPaused", producer->paused, 0, pool));
  RCC(rc, finish, jbn_clone(consumer->rtp_parameters, &n, pool));
  jbn_add_item(resp, n);

  RCC(rc, finish, iwulist_push(&member->resource_refs, &(struct rct_resource_ref) {
    .b = rct_resource_ref_lk(consumer, 1, __func__),
  }));
  RCC(rc, finish, iwhmap_put(_map_resource_member,
                             (void*) (uintptr_t) consumer->id, (void*) (uintptr_t) member->id));
  rct_unlock(), locked = false;

  RCC(rc, finish, jbl_from_node(&jbl, resp));
  if (_send_to_member(member_id, jbl, __func__)) {
    jbl_destroy(&jbl);
  }

finish:
  rct_resource_ref_keep_locking(producer_member, locked, -1, __func__);
  rct_resource_ref_keep_locking(consumer, locked, -1, __func__);
  rct_resource_ref_unlock(member, locked, -1, __func__);
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_warn3(rc);
  }
  return rc;
}

static iwrc _transport_produce(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
      kind: RTP_KIND_VIDEO | RTP_KIND_AUDIO
      paused: boolean
      rtpParameters: {...}
     }
     Response: {
      id: Transport uuid
     }
   */
  iwrc rc = 0;
  const char *error = 0;

  uint32_t kind;
  wrc_resource_t producer_id, transport_id, observer_id = 0;
  JBL_NODE n = 0, rtp_parameters;

  bool locked = false, paused = false;
  rct_room_member_t *member = 0;
  rct_transport_webrtc_t *transport = 0;
  rct_producer_t *producer = 0;
  rct_producer_spec_t *producer_spec;

  IWULIST member_ids;
  RCC(rc, finish, iwulist_init(&member_ids, 64, sizeof(wrc_resource_t)));
  jbn_at(ctx->payload, "/kind", &n);
  CHECK_JBN_TYPE(n, finish, JBV_I64);
  kind = n->vi64;

  jbn_at(ctx->payload, "/paused", &n);
  if (n && (n->type == JBV_BOOL)) {
    paused = n->vbool;
  }

  RCC(rc, finish, jbn_at(ctx->payload, "/rtpParameters", &rtp_parameters));
  CHECK_JBN_TYPE(rtp_parameters, finish, JBV_OBJECT);

  member = rct_resource_by_id_locked(_wss_member_get(ctx->wss), RCT_TYPE_ROOM_MEMBER, __func__);
  locked = true;

  transport = rct_resource_ref_lk(
    _rct_member_findref_by_flag_lk(member, MRES_SEND_TRANSPORT),
    1, __func__);
  if (!transport) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    goto finish;
  }
  transport_id = transport->id;

  // Find audio level observer
  if ((kind & RTP_KIND_AUDIO) && (member->room->flags & RCT_ROOM_ALO)) {
    for (struct rct_rtp_observer *o = member->room->router->observers; o; o = o->next) {
      if (o->type & RCT_TYPE_OBSERVER_AL) {
        observer_id = o->id;
        break;
      }
    }
  }
  rct_unlock(), locked = false;
  // +1 for transport & member

  RCC(rc, finish, rct_producer_spec_create2(kind, rtp_parameters, &producer_spec));
  producer_spec->paused = paused;
  RCC(rc, finish, rct_producer_create(transport_id, producer_spec, &producer_id));

  rct_lock(), locked = true;
  producer = rct_resource_by_id_locked_lk(producer_id, RCT_TYPE_PRODUCER, __func__);
  if (!producer) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    goto finish;
  }

  RCC(rc, finish, iwhmap_put(_map_resource_member,
                             (void*) (uintptr_t) producer->id, (void*) (uintptr_t) member->id));
  // Collect room members
  for (rct_room_member_t *m = member->room->members; m; m = m->next) {
    if (m != member) {
      iwulist_push(&member_ids, &m->id);
    }
  }
  rc = iwulist_push(&member->resource_refs, &(struct rct_resource_ref) {
    .b = rct_resource_ref_lk(producer, 1, __func__),
  });
  if (rc) {
    rct_resource_ref_lk(producer, -1, __func__);
    goto finish;
  }
  rct_unlock(), locked = false;

  for (int i = 0, l = iwulist_length(&member_ids); i < l; ++i) {
    wrc_resource_t id = *(wrc_resource_t*) iwulist_at2(&member_ids, i);
    _consumer_create(id, producer->id);
  }

  if (observer_id) {
    RCC(rc, finish, rct_observer_add_producer(observer_id, producer_id));
  }

  RCC(rc, finish, jbn_from_json("{}", &n, ctx->pool));
  RCC(rc, finish, jbn_add_item_str(n, "id", producer->uuid, IW_UUID_STR_LEN, 0, ctx->pool));

finish:
  rct_resource_ref_keep_locking(producer, locked, -1, __func__);
  rct_resource_ref_keep_locking(transport, locked, -1, __func__);
  rct_resource_ref_unlock(member, locked, -1, __func__);
  iwulist_destroy_keep(&member_ids);

  SIMPLE_HANDLER_FINISH_RET(n);
}

static iwrc _consumer_pause_resume(struct ws_message_ctx *ctx, bool is_resume) {
  /* Payload: {
      id: consumer uuid
     } */
  iwrc rc = 0;
  const char *error = 0;

  JBL_NODE n;
  rct_resource_base_t b;

  RCC(rc, finish, jbn_at(ctx->payload, "/id", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);

  RCC(rc, finish, rct_resource_probe_by_uuid(n->vptr, &b));

  if (is_resume) {
    rc = rct_consumer_resume(b.id);
  } else {
    rc = rct_consumer_pause(b.id);
  }

finish:
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _consumer_resume(struct ws_message_ctx *ctx, void *op) {
  return _consumer_pause_resume(ctx, true);
}

static iwrc _consumer_pause(struct ws_message_ctx *ctx, void *op) {
  return _consumer_pause_resume(ctx, false);
}

static iwrc _consumer_set_priority(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {
      id: consumer uuid
      priority: integer
     } */
  iwrc rc = 0;
  const char *error = 0;

  JBL_NODE n, priority;
  rct_resource_base_t b;

  RCC(rc, finish, jbn_at(ctx->payload, "/id", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  RCC(rc, finish, jbn_at(ctx->payload, "/priority", &priority));
  CHECK_JBN_TYPE(n, finish, JBV_I64);

  RCC(rc, finish, rct_resource_probe_by_uuid(n->vptr, &b));
  rc = rct_consumer_set_priority(b.id, priority->vi64);

finish:
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _consumer_set_preferred_layers(struct ws_message_ctx *ctx, void *op) {
  /*   Payload: {
     id: consumer uuid,
     spartial: integer | undefined,
     temporal: integer | uncefined
     } */
  iwrc rc = 0;
  const char *error = 0;

  JBL_NODE n;
  rct_resource_base_t b;
  rct_consumer_layer_t layer = { 0 };

  RCC(rc, finish, jbn_at(ctx->payload, "/id", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);

  RCC(rc, finish, rct_resource_probe_by_uuid(n->vptr, &b));

  if (!jbn_at(ctx->payload, "/spartial", &n) && (n->type == JBV_I64)) {
    layer.spartial = n->vi64;
  }
  if (!jbn_at(ctx->payload, "/temporal", &n) && (n->type == JBV_I64)) {
    layer.temporal = n->vi64;
  }

  rc = rct_consumer_set_preferred_layers(b.id, layer);

finish:
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _acquire_room_streams(struct ws_message_ctx *ctx, void *op) {
  /* Payload: {} */
  IWULIST slots;
  rct_room_member_t *member;

  const char *error = 0;
  bool locked = false;
  wrc_resource_t member_id = _wss_member_get(ctx->wss);

  iwrc rc = iwulist_init(&slots, 128, sizeof(wrc_resource_t));
  RCGO(rc, finish);

  rct_lock(), locked = true;
  member = rct_resource_by_id_unsafe(member_id, RCT_TYPE_ROOM_MEMBER);
  if (!member) {
    rc = GR_ERROR_RESOURCE_NOT_FOUND;
    goto finish;
  }
  for (rct_room_member_t *m = member->room->members; m; m = m->next) {
    if (m != member) {
      for (int i = 0, l = iwulist_length(&m->resource_refs); i < l; ++i) {
        struct rct_resource_ref *rr = iwulist_at2(&m->resource_refs, i);
        if (rr->b->type == RCT_TYPE_PRODUCER) {
          iwulist_push(&slots, &rr->b->id);
        }
      }
    }
  }
  rct_unlock(), locked = false;

  for (int i = 0, l = iwulist_length(&slots); i < l; ++i) {
    wrc_resource_t producer_id = *(wrc_resource_t*) iwulist_at2(&slots, i);
    _consumer_create(member_id, producer_id);
  }

finish:
  if (locked) {
    rct_unlock();
  }
  iwulist_destroy_keep(&slots);
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _producer_close_pause_resume(struct ws_message_ctx *ctx, bool is_close, bool is_resume) {
  /* Payload: {
      id: producer uuid
     }
   */
  JBL_NODE n;
  rct_resource_base_t b;
  iwrc rc = 0;
  const char *error = 0;

  jbn_at(ctx->payload, "/id", &n);
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  RCC(rc, finish, rct_resource_probe_by_uuid(n->vptr, &b));

  if (is_close) {
    rc = rct_producer_close(b.id);
  } else if (is_resume) {
    rc = rct_producer_resume(b.id);
  } else {
    rc = rct_producer_pause(b.id);
  }

finish:
  if (is_close && (rc == IW_ERROR_NOT_EXISTS)) {
    rc = 0;
  }
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _producer_close(struct ws_message_ctx *ctx, void *op) {
  return _producer_close_pause_resume(ctx, true, false);
}

static iwrc _producer_pause(struct ws_message_ctx *ctx, void *op) {
  return _producer_close_pause_resume(ctx, false, false);
}

static iwrc _producer_resume(struct ws_message_ctx *ctx, void *op) {
  return _producer_close_pause_resume(ctx, false, true);
}

static iwrc _transport_restart_ice(struct ws_message_ctx *ctx, void *op) {
  JBL_NODE n;
  rct_resource_base_t b;
  iwrc rc = 0;
  const char *error = 0;

  jbn_at(ctx->payload, "/id", &n);
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  RCC(rc, finish, rct_resource_probe_by_uuid(n->vptr, &b));
  rc = rct_transport_webrtc_restart_ice(b.id, 0);

finish:
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _room_info_set_for_member(
  rct_room_t    *room,
  wrc_resource_t member_id,
  const char    *name
  ) {
  iwrc rc = 0;
  int64_t user_id;
  EJDB_LIST list = 0;
  JBL_NODE n, n2, n3;

  rct_room_member_t *member = rct_resource_by_id_locked_exists(member_id, RCT_TYPE_ROOM_MEMBER, __func__);
  if (!member) {
    return 0;
  }
  user_id = member->user_id;
  rct_resource_unlock(member, __func__);

  JQL q = 0;
  RCC(rc, finish, jql_create(&q, "users", "/= :? | /rooms"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, user_id));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));
  if (!list->first || jbn_at(list->first->node, "/rooms", &n) || n->type != JBV_ARRAY) {
    goto finish;
  }
  for (JBL_NODE nn = n->child; nn; nn = nn->next) {
    if (!jbn_at(nn, "/cid", &n2) && n2->type == JBV_STR && strcmp(n2->vptr, room->cid) == 0) {
      if (!jbn_at(nn, "/name", &n3) && n3->type == JBV_STR && strcmp(n3->vptr, name) != 0) {
        n3->vptr = name;
        n3->vsize = (int) strlen(name);
        RCC(rc, finish, ejdb_patch_jbn(g_env.db, "users", list->first->node, list->first->id));
      }
      break;
    }
  }

finish:
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  return rc;
}

/// Propagate room info update to its members.
static iwrc _room_info_set_apply(
  struct ws_message_ctx *ctx,
  rct_room_t            *room,
  IWULIST               *mlist,
  const char            *name,
  const char            *name_old
  ) {
  iwrc rc = 0;
  JQL q = 0;
  JBL_NODE n, n2;
  uint64_t ts;
  int namelen = (int) strlen(name);

  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  // Update room name and events history
  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
  RCC(rc, finish, jbn_from_json_printf(
        &n, ctx->pool,
        "["
        "{\"op\":\"replace\", \"path\":\"/name\", \"value\":\"\"},"
        "{\"op\":\"add\", \"path\":\"/events/-\", "
        "\"value\":[\"renamed\", %" PRIu64 ", \"\", \"\"]}"
        "]",
        ts));

  if (!jbn_at(n, "/0/value", &n2) && n2->type == JBV_STR) {
    n2->vptr = name;
    n2->vsize = namelen;
  }
  if (!jbn_at(n, "/1/value/2", &n2) && n2->type == JBV_STR) {
    n2->vptr = name_old;
    n2->vsize = (int) strlen(name_old);
  }
  if (!jbn_at(n, "/1/value/3", &n2) && n2->type == JBV_STR) {
    n2->vptr = name;
    n2->vsize = namelen;
  }
  RCC(rc, finish, jql_set_str(q, 0, 0, room->uuid));
  RCC(rc, finish, jql_set_json(q, 0, 1, n));
  RCC(rc, finish, ejdb_update(g_env.db, q));

  // Patch rooms history of every room participant
  for (int i = 0; i < mlist->num; ++i) {
    wrc_resource_t id = *(wrc_resource_t*) iwulist_at2(mlist, i);
    iwrc rc = _room_info_set_for_member(room, id, name);
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }

finish:
  jql_destroy(&q);
  return rc;
}

static iwrc _room_info_set(struct ws_message_ctx *ctx, void *op) {
  iwrc rc = 0;
  const char *error = 0;
  char *name_old = 0;
  bool locked = false;
  JBL_NODE n = 0;
  rct_room_member_t *member = 0;
  rct_room_t *room = 0;
  JBL jbl;
  IWULIST mlist = { 0 };
  wrc_resource_t member_id = _wss_member_get(ctx->wss);

  jbn_at(ctx->payload, "/name", &n);
  if (!n || n->type != JBV_STR || n->vsize < 1 || n->vsize > 255) {
    return GR_ERROR_COMMAND_INVALID_INPUT;
  }
  char name[n->vsize + 1]; // Name is limited to 255, so it's safe.

  rct_lock(), locked = true;
  member = rct_resource_by_id_unsafe(member_id, RCT_TYPE_ROOM_MEMBER);
  if (!member || !member->room) {
    goto finish;
  }
  room = rct_resource_ref_lk(member->room, 1, __func__);
  if (member->user_id != room->owner_user_id || strcmp(room->name, n->vptr) == 0) {
    goto finish;
  }
  char *p = strdup(n->vptr);
  RCA(p, finish);

  name_old = room->name;
  room->name = p;

  memcpy(name, n->vptr, n->vsize);
  name[n->vsize] = '\0';

  RCC(rc, finish, iwulist_init(&mlist, 32, sizeof(wrc_resource_t)));
  for (rct_room_member_t *m = room->members; m; m = m->next) {
    iwulist_push(&mlist, &m->id);
  }
  rct_unlock(), locked = false;

  RCC(rc, finish, _room_info_set_apply(ctx, room, &mlist, name, name_old));

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  jbl_set_string(jbl, "cmd", "room_info");
  jbl_set_string(jbl, "room", room->uuid);
  jbl_set_string(jbl, "name", name);
  if (_send_to_members(room->id, 0, jbl, __func__)) {
    jbl_destroy(&jbl);
  }

finish:
  rct_resource_ref_keep_locking(room, locked, -1, __func__);
  if (locked) {
    rct_unlock();
  }
  free(name_old);
  iwulist_destroy_keep(&mlist);
  SIMPLE_HANDLER_FINISH_RET(0);
}

static iwrc _member_info_set(struct ws_message_ctx *ctx, void *op) {
  iwrc rc = 0;
  const char *error = 0;
  JBL jbl = 0;
  JBL_NODE n;
  char uuid[IW_UUID_STR_LEN + 1];
  wrc_resource_t member_id = _wss_member_get(ctx->wss);
  rct_room_member_t *member = 0;

  jbn_at(ctx->payload, "/name", &n);
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  if (n->vsize < 1 || n->vsize > 255) {
    rc = GR_ERROR_COMMAND_INVALID_INPUT;
    goto finish;
  }

  rct_lock();
  member = rct_resource_by_id_unsafe(member_id, RCT_TYPE_ROOM_MEMBER);
  if (member) {
    char *name = strdup(n->vptr);
    if (name) {
      free(member->name);
      member->name = name;
    }
    memcpy(uuid, member->uuid, IW_UUID_STR_LEN);
    uuid[IW_UUID_STR_LEN] = '\0';
  }
  rct_unlock();

  if (member) {
    RCC(rc, finish, jbl_create_empty_object(&jbl));
    RCC(rc, finish, jbl_set_string(jbl, "cmd", "member_info"));
    RCC(rc, finish, jbl_set_string(jbl, "member", uuid));
    RCC(rc, finish, jbl_set_string(jbl, "name", n->vptr));
    rc = _send_to_members2(member_id, jbl, false, __func__);
  }

finish:
  if (rc) {
    jbl_destroy(&jbl);
  }
  SIMPLE_HANDLER_FINISH_RET(0);
}

#if (ENABLE_RECORDING == 1)

static iwrc _recording(struct ws_message_ctx *ctx, void *op) {
  iwrc rc = 0;
  const char *error = 0;
  bool locked = false, has_started_recording = false;
  rct_room_member_t *member = 0;
  wrc_resource_t member_id = _wss_member_get(ctx->wss), room_id;

  rct_lock(), locked = true;
  member = rct_resource_by_id_unsafe(member_id, RCT_TYPE_ROOM_MEMBER);
  if (member == 0) {
    error = "error.not_a_room_member";
    goto finish;
  }
  if (member->room->owner_user_id != member->user_id) {
    error = "error.insufficient_permissions";
    goto finish;
  }
  room_id = member->room->id;
  has_started_recording = member->room->has_started_recording;
  rct_unlock(), locked = false;

  if (!has_started_recording) {
    rc = rct_room_recording_start(room_id);
  } else {
    rc = rct_room_recording_stop(room_id);
  }

finish:
  if (locked) {
    rct_unlock();
  }
  SIMPLE_HANDLER_FINISH_RET(0);
}

#endif

#if (ENABLE_WHITEBOARD == 1)

static iwrc _whiteboard_open(struct ws_message_ctx *ctx, void *op) {
  iwrc rc = 0;
  bool locked = false;
  rct_room_member_t *member = 0;
  wrc_resource_t member_id = _wss_member_get(ctx->wss), room_id;
  JBL jbl = 0;
  JQL q = 0;
  JBL_NODE n = 0, n2 = 0;
  uint64_t ts;

  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  rct_lock(), locked = true;
  member = rct_resource_by_id_unsafe(member_id, RCT_TYPE_ROOM_MEMBER);
  if (!member) {
    rc = IW_ERROR_INVALID_STATE;
    goto finish;
  }

  room_id = member->room->id;

  if (  (member->room->owner_user_id == member->user_id || (member->room->flags & RCT_ROOM_MEETING))
     && (member->room->num_whiteboard_clicks)++ == 0) {
    RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
    RCC(rc, finish, jbn_from_json_printf(
          &n, ctx->pool,
          "["
          "{\"op\":\"add\", \"path\":\"/events/-\", "
          "\"value\":[\"whiteboard\", %" PRIu64 "]}"
          "]",
          ts));

    if (!jbn_at(n, "/0/value", &n2) && n2->type == JBV_ARRAY) {
      jbn_add_item_str(n2, 0, member->name, -1, 0, ctx->pool);
      jbn_add_item_str(n2, 0, member->room->whiteboard_link, -1, 0, ctx->pool);
    }
    RCC(rc, finish, jql_set_str(q, 0, 0, member->room->uuid));
    RCC(rc, finish, jql_set_json(q, 0, 1, n));

    RCC(rc, finish, jbl_create_empty_object(&jbl));
    RCC(rc, finish, jbl_set_string(jbl, "event", "ROOM_WHITEBOARD_INIT"));
    RCC(rc, finish, jbl_set_string(jbl, "room", member->room->uuid));
    RCC(rc, finish, jbl_set_string(jbl, "member", member->uuid));
    RCC(rc, finish, jbl_set_string(jbl, "name", member->name));
  }

  rct_unlock(), locked = false;

  if (q != 0) {
    RCC(rc, finish, ejdb_update(g_env.db, q));
  }

  if (jbl != 0) {
    RCC(rc, finish, _send_to_members(room_id, member_id, jbl, __func__));
  }

finish:
  if (locked) {
    rct_unlock();
  }
  jql_destroy(&q);
  if (rc) {
    // If rc == 0 it will be destroyed in _send_to_members
    jbl_destroy(&jbl);
  }
  return rc;
}

#endif

static void _collect_dependent_resource_ids_lk(void *b, IWULIST *idlist) {
  if (!b) {
    return;
  }
  switch (((rct_resource_base_t*) b)->type) {
    case RCT_TYPE_ROUTER: {
      rct_router_t *r = b;
      for (struct rct_transport *v = r->transports; v; v = v->next) {
        iwulist_push(idlist, &v->id);
        _collect_dependent_resource_ids_lk(v, idlist);
      }
      if (r->room) {
        iwulist_push(idlist, &r->room->id);
      }
      break;
    }
    case RCT_TYPE_TRANSPORT_WEBRTC:
    case RCT_TYPE_TRANSPORT_PLAIN:
    case RCT_TYPE_TRANSPORT_DIRECT:
    case RCT_TYPE_TRANSPORT_PIPE: {
      rct_transport_t *t = b;
      for (rct_producer_base_t *v = t->producers; v; v = v->next) {
        iwulist_push(idlist, &v->id);
        _collect_dependent_resource_ids_lk(v, idlist);
      }
      break;
    }
    case RCT_TYPE_PRODUCER:
    case RCT_TYPE_PRODUCER_DATA: {
      rct_producer_t *p = b;
      for (rct_consumer_base_t *v = p->consumers; v; v = v->next) {
        iwulist_push(idlist, &v->id);
      }
      break;
    }
  }
}

static void _resource_closed_notify_member_lk(rct_room_member_t *member, rct_resource_base_t *b) {
  JBL jbl;
  iwrc rc = jbl_from_json(&jbl, "{}");
  RCGO(rc, finish);
  jbl_set_string(jbl, "event", "RESOURCE_CLOSED");
  jbl_set_int64(jbl, "type", b->type);
  jbl_set_string(jbl, "id", b->uuid);
  if (_send_to_member(member->id, jbl, __func__)) {
    jbl_destroy(&jbl);
  }
finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _on_resource_closed(wrc_resource_t resource_id) {
  iwrc rc = 0;
  IWULIST idlist;
  RCC(rc, finish, iwulist_init(&idlist, 64, sizeof(wrc_resource_t)));

  iwulist_push(&idlist, &resource_id);
  rct_resource_base_t *b = rct_resource_by_id_locked(resource_id, 0, __func__);
  if (b) {
    iwlog_debug("room_on_resource_closed() ROOT %" PRIu32 " %s %s",
                b->id, b->uuid, rct_resource_type_name(b->type));
    _collect_dependent_resource_ids_lk(b, &idlist);
  }
  rct_resource_unlock(b, __func__);

  for (int i = 0, l = iwulist_length(&idlist); i < l; ++i) {
    rct_room_member_t *member = 0;
    resource_id = *(wrc_resource_t*) iwulist_at2(&idlist, i);
    rct_lock();
    wrc_resource_t member_id = (uintptr_t) iwhmap_get(_map_resource_member, (void*) (uintptr_t) resource_id);
    if (member_id) {
      member = rct_resource_by_id_unsafe(member_id, RCT_TYPE_ROOM_MEMBER);
      iwhmap_remove(_map_resource_member, (void*) (uintptr_t) resource_id);
    }
    if (member) {
      for (int i = 0, l = iwulist_length(&member->resource_refs); i < l; ++i) {
        struct rct_resource_ref *ref = iwulist_at2(&member->resource_refs, i);
        if (ref->b->id == resource_id) {
          iwlog_debug("room_on_resource_closed() DEP %" PRIu32 " %s %s",
                      ref->b->id, ref->b->uuid, rct_resource_type_name(ref->b->type));
          _resource_closed_notify_member_lk(member, ref->b);
          rct_resource_ref_lk(ref->b, -1, __func__); // Unref -1
          iwulist_remove(&member->resource_refs, i);
          l = iwulist_length(&member->resource_refs);
          --i;
        }
      }
    }
    rct_unlock();
  }

finish:
  iwulist_destroy_keep(&idlist);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _on_resource_score(wrc_resource_t resource_id, JBL data) {
  JBL event = 0, event_data = 0;
  const char *event_name = 0;
  bool locked = false;
  /* "data": [
      { "encodingIdx": 0,
        "score": 10,
        "ssrc": 484176009
      }],
     "event": "score",
     "targetId": "ce983fcc-4d71-419c-b045-006b4ef143e9" */
  iwrc rc = jbl_create_empty_object(&event);
  RCGO(rc, finish);
  RCC(rc, finish, jbl_at(data, "/data", &event_data));

  rct_lock(), locked = true;
  wrc_resource_t member_id = (uintptr_t) iwhmap_get(_map_resource_member, (void*) (uintptr_t) resource_id);
  rct_resource_base_t *b = rct_resource_by_id_unsafe(resource_id, 0);
  if (b) {
    if (b->type & RCT_TYPE_CONSUMER_ALL) {
      event_name = "CONSUMER_SCORE";
    } else if (b->type & RCT_TYPE_PRODUCER_ALL) {
      event_name = "PRODUCER_SCORE";
    }
    RCC(rc, finish, jbl_set_string(event, "id", b->uuid));
  }
  rct_unlock(), locked = false;

  if (event_name) {
    RCC(rc, finish, jbl_set_string(event, "event", event_name));
    RCC(rc, finish, jbl_set_nested(event, "score", event_data));
    if (_send_to_member(member_id, event, __func__)) {
      jbl_destroy(&event);
    }
  } else {
    jbl_destroy(&event);
  }

finish:
  if (locked) {
    rct_unlock();
  }
  jbl_destroy(&event_data);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _on_consumer_layer_change(wrc_resource_t resource_id, JBL data) {
  JBL event_data = 0, jbl = 0;
  iwrc rc = jbl_at(data, "/data", &jbl);
  if (rc || jbl_type(jbl) != JBV_OBJECT) {
    jbl_destroy(&jbl);
    return;
  }
  RCC(rc, finish, jbl_clone(jbl, &event_data));

  rct_lock();
  wrc_resource_t member_id = (uintptr_t) iwhmap_get(_map_resource_member, (void*) (uintptr_t) resource_id);
  rct_resource_base_t *b = rct_resource_by_id_unsafe(resource_id, 0);
  if (b) {
    jbl_set_string(event_data, "id", b->uuid);
  }
  rct_unlock();

  if (member_id && b) {
    jbl_set_string(event_data, "event", "CONSUMER_LAYERS_CHANGED");
    if (_send_to_member(member_id, event_data, __func__)) {
      jbl_destroy(&event_data);
    }
  } else {
    jbl_destroy(&event_data);
  }

finish:
  jbl_destroy(&jbl);
  if (rc) {
    jbl_destroy(&event_data);
    iwlog_ecode_error3(rc);
  }
}

static void _on_consumer_paused_changed(wrc_resource_t resource_id, bool paused) {
  JBL event_data = 0;
  iwrc rc = jbl_create_empty_object(&event_data);
  RCGO(rc, finish);

  rct_lock();
  wrc_resource_t member_id = (uintptr_t) iwhmap_get(_map_resource_member, (void*) (uintptr_t) resource_id);
  rct_consumer_t *consumer = rct_resource_by_id_unsafe(resource_id, RCT_TYPE_CONSUMER_ALL);
  if (consumer) {
    jbl_set_string(event_data, "id", consumer->uuid);
  }
  rct_unlock();

  if (member_id && consumer) {
    if (paused) {
      jbl_set_string(event_data, "event", "CONSUMER_PAUSED");
    } else {
      jbl_set_string(event_data, "event", "CONSUMER_RESUMED");
    }
    if (_send_to_member(member_id, event_data, __func__)) {
      jbl_destroy(&event_data);
    }
  } else {
    jbl_destroy(&event_data);
  }

finish:
  if (rc) {
    jbl_destroy(&event_data);
    iwlog_ecode_error3(rc);
  }
}

void _on_alo_volumes(wrc_resource_t resource_id, JBL data) {
  iwrc rc = 0;
  JBL_NODE jbn, at, items;
  bool locked = false;
  wrc_resource_t room_id = 0;
  IWPOOL *pool = iwpool_create(jbl_size(data) * 2);
  RCA(pool, finish);

  RCC(rc, finish, jbl_to_node(data, &jbn, true, pool));
  RCC(rc, finish, jbn_at(jbn, "/targetId", &at));
  jbn_remove_item(jbn, at);

  RCC(rc, finish, jbn_at(jbn, "/event", &at));
  at->vptr = "VOLUMES";
  at->vsize = sizeof("VOLUMES") - 1;

  RCC(rc, finish, jbn_at(jbn, "/data", &items));
  if (items->type != JBV_ARRAY) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }

  rct_lock();
  rct_rtp_observer_t *o = rct_resource_by_id_unsafe(resource_id, RCT_TYPE_OBSERVER_AL);
  if (o) {
    rct_room_t *room = o->router->room;
    if (room) {
      room_id = room->id;
    }
  }
  rct_unlock();
  if (!room_id) {
    return;
  }

  // "data": [
  //    {
  //      "producerId": "4fec19a8-93da-448c-9ced-13fd38475c66",
  //      "volume": -42
  //    },
  //   ...
  // ],
  // "event": "volumes",
  // "targetId": "a45e2441-c05e-46d6-a6b3-0fdc2620ff84"

  rct_lock(), locked = true;

  for (JBL_NODE n = items->child; n; n = n->next) {
    if (n->type != JBV_OBJECT) {
      continue;
    }
    JBL_NODE pn, vn;
    RCC(rc, finish, jbn_at(n, "/producerId", &pn));
    RCC(rc, finish, jbn_at(n, "/volume", &vn));
    if ((pn->type != JBV_STR) || (vn->type != JBV_I64)) {
      rc = GR_ERROR_INVALID_DATA_RECEIVED;
      goto finish;
    }
    n->type = JBV_ARRAY;
    pn->key = vn->key = 0;
    int i = 0;
    for (JBL_NODE nn = n->child; nn; nn = nn->next) {
      nn->klidx = i++;
    }
    rct_resource_base_t *b = rct_resource_by_uuid_unsafe(pn->vptr, RCT_TYPE_PRODUCER);
    if (b) {
      wrc_resource_t mid = (wrc_resource_t) (uintptr_t) iwhmap_get(_map_resource_member, (void*) (uintptr_t) b->id);
      b = rct_resource_by_id_unsafe(mid, RCT_TYPE_ROOM_MEMBER);
    }
    if (b) {
      pn->vptr = iwpool_strndup(pool, b->uuid, IW_UUID_STR_LEN, &rc);
      RCGO(rc, finish);
    } else {
      pn->vptr = "";
      pn->vsize = 1;
    }
  }
  rct_unlock(), locked = false;

  struct send_task *t = malloc(sizeof(*t));
  RCA(t, finish);

  *t = (struct send_task) {
    .room_id = room_id,
    .jbn = jbn,
    .pool = pool,
    .tag = __func__
  };
  RCC(rc, finish, iwtp_schedule(g_env.tp, _send_to_task, t));
  pool = 0; // pool will be destroyed by _send_to_task

finish:
  if (locked) {
    rct_unlock();
  }
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  iwpool_destroy(pool);
}

#if (ENABLE_RECORDING == 1)

void _on_recording(wrc_resource_t room_id, bool recording) {
  iwrc rc = 0;
  JQL q = 0;
  JBL jbl = 0;
  JBL_NODE patch, patch_op, patch_value;
  uint64_t ts;
  rct_resource_base_t b;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  RCC(rc, finish, jbn_from_json("[]", &patch, pool));
  RCC(rc, finish, jbn_add_item_obj(patch, 0, &patch_op, pool));
  RCC(rc, finish, jbn_add_item_str(patch_op, "op", "add", sizeof("add") - 1, 0, pool));
  RCC(rc, finish, jbn_add_item_str(patch_op, "path", "/events/-", sizeof("/events/-") - 1, 0, pool));
  RCC(rc, finish, jbn_add_item_arr(patch_op, "value", &patch_value, pool));

  RCC(rc, finish, jbn_add_item_str(patch_value, 0,
                                   recording ? "recstart" : "recstop",
                                   (recording ? sizeof("recstart") : sizeof("recstop")) - 1, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(patch_value, 0, (int64_t) ts, 0, pool));

  RCC(rc, finish, rct_resource_probe_by_id(room_id, &b));
  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] | apply :?"));
  RCC(rc, finish, jql_set_str(q, 0, 0, b.uuid));
  RCC(rc, finish, jql_set_json(q, 0, 1, patch));

  RCC(rc, finish, ejdb_update(g_env.db, q));

  // Send event to room participants
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "event", recording ? "ROOM_RECORDING_ON" : "ROOM_RECORDING_OFF"));
  RCC(rc, finish, jbl_set_string(jbl, "room", b.uuid));
  if (_send_to_members(room_id, 0, jbl, __func__)) {
    jbl_destroy(&jbl);
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    jbl_destroy(&jbl);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
}

#endif

static iwrc _rct_event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  switch (evt) {
    case WRC_EVT_ROOM_CREATED:
      _on_room_created(resource_id);
      break;
    case WRC_EVT_ROOM_CLOSED:
      _on_room_closed(data);
      break;
    case WRC_EVT_ROOM_MEMBER_JOIN:
      _on_member_join(resource_id);
      break;
    case WRC_EVT_ROOM_MEMBER_LEFT:
      _on_member_left(data);
      break;
    case WRC_EVT_TRANSPORT_CLOSED:
    case WRC_EVT_PRODUCER_CLOSED:
    case WRC_EVT_CONSUMER_CLOSED:
      _on_resource_closed(resource_id);
      break;
    case WRC_EVT_RESOURCE_SCORE:
      _on_resource_score(resource_id, data);
      break;
    case WRC_EVT_CONSUMER_LAYERSCHANGE:
      _on_consumer_layer_change(resource_id, data);
      break;
    case WRC_EVT_CONSUMER_PAUSE:
      _on_consumer_paused_changed(resource_id, true);
      break;
    case WRC_EVT_CONSUMER_RESUME:
      _on_consumer_paused_changed(resource_id, false);
      break;
    case WRC_EVT_AUDIO_OBSERVER_VOLUMES:
      _on_alo_volumes(resource_id, data);
      break;
#if (ENABLE_RECORDING == 1)
    case WRC_EVT_ROOM_RECORDING_ON:
      _on_recording(resource_id, true);
      break;
    case WRC_EVT_ROOM_RECORDING_OFF:
      _on_recording(resource_id, false);
      break;
#endif
    default:
      break;
  }
  return 0;
}

static void _wss_closed_listener(int event, struct ws_session *wss, void *data) {
  if (event == WS_EVENT_CLOSE) {
    iwrc rc = _room_leave_wss(wss);
    if (rc) {
      iwlog_ecode_warn3(rc);
    }
  }
}

// Static module refs
static uint32_t _event_handler_id;

iwrc rct_room_module_init(void) {
  iwrc rc = 0;
  RCB(finish, _map_resource_member = iwhmap_create_i64(0));

  RCC(rc, finish, wrc_add_event_handler(_rct_event_handler, 0, &_event_handler_id));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "room_create", "rct_room::room_create", _room_create, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "room_leave", "rct_room::room_leave", _room_leave, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "room_message", "rct_room::room_message", _room_message, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "room_messages", "rct_room::room_messages", _room_messages, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "room_info_get", "rct_room::room_info_get", _room_info_get, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "rtp_capabilities", "rct_room::rtp_capabilities", _rtp_capabilities, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "transports_init", "rct_room::transports_init", _transports_init, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "transport_connect", "rct_room::transport_connect", _transport_connect, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "transport_produce", "rct_room::transport_produce", _transport_produce, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "consumer_resume", "rct_room::consumer_resume", _consumer_resume, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "consumer_pause", "rct_room::consumer_pause", _consumer_pause, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "consumer_created", "rct_room::consumer_resume", _consumer_resume, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "consumer_set_priority", "rct_room::consumer_set_priority", _consumer_set_priority, 0, 0));
  RCC(rc,
      finish,
      grh_ws_register_wsh_handler(
        "consumer_set_preferred_layers", "rct_room::consumer_set_preferred_layers", _consumer_set_preferred_layers, 0,
        0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "acquire_room_streams", "rct_room::acquire_room_streams", _acquire_room_streams, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "producer_close", "rct_room::producer_close", _producer_close, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "producer_pause", "rct_room::producer_pause", _producer_pause, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "producer_resume", "rct_room::producer_resume", _producer_resume, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "transport_restart_ice", "rct_room::transport_restart_ice", _transport_restart_ice, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "member_info_set", "rct_room::member_info_set", _member_info_set, 0, 0));
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "room_info_set", "rct_room::room_info_set", _room_info_set, 0, 0));
#if (ENABLE_RECORDING == 1)
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "recording", "rct_room::recording", _recording, 0, 0));
#endif
#if (ENABLE_WHITEBOARD == 1)
  RCC(rc, finish, grh_ws_register_wsh_handler(
        "whiteboard_open", "rct_room::whiteboard_open", _whiteboard_open, 0, 0));
#endif

  rc = rct_room_recording_module_init();

finish:
  return rc;
}

void rct_room_module_close(void) {
  wrc_remove_event_handler(_event_handler_id);
  rct_room_recording_module_close();
  iwhmap_destroy(_map_resource_member);
}
