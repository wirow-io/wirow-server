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

#include <ejdb2/ejdb2.h>
#include <iowow/iwxstr.h>
#include <iowow/iwhmap.h>
#include <iowow/iwarr.h>
#include <iwnet/iwn_ws_server.h>

#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define WS_TICKET_LEN IW_UUID_STR_LEN

static pthread_rwlock_t _rwl;

#define RLOCK() pthread_rwlock_rdlock(&_rwl)

#define WLOCK() pthread_rwlock_wrlock(&_rwl)

#define UNLOCK() pthread_rwlock_unlock(&_rwl)

static iwrc _init(void);

// Mapping WS session id wsid (int) -> *wsdata
static IWHMAP *_map_wsid_wsdata;

// Mapping ws session uuid (char*) -> *wsdata
static IWHMAP *_map_uuid_sessions;

static IWHMAP *_wsh_handlers;

static pthread_mutex_t _handlers_mtx;

struct wsh_handler {
  wsh_handler_fn wsh;
  const char    *name;
  void *data;
  void  (*dispose)(void *data);
  struct wsh_handler *next;
};

struct ws_event_listener_slot {
  ws_event_listener_fn listener;
  void *data;
  struct ws_event_listener_slot *next;
};

static iwrc _ws_ticket_pull(struct ws_session *wss, const char *ticket) {
  JQL q = 0;
  EJDB_LIST list = 0;
  const char *val;

  iwrc rc = jql_create(&q, "tickets", "/[name = :?] | del");
  RCGO(rc, finish);
  RCC(rc, finish, jql_set_str(q, 0, 0, ticket));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));

  if (!list->first) {
    rc = GR_ERROR_UNKNOWN_TICKET_ID;
    goto finish;
  }

  RCC(rc, finish, jbl_object_get_str(list->first->raw, "session_id", &val));
  RCC(rc, finish, iwn_wf_session_id_set(wss->ws->req, val));
  grh_auth_request_init(wss->ws->req);

finish:
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  return rc;
}

iwrc grh_ws_visit_clients(ws_clients_visitor visitor, void *data) {
  IWHMAP_ITER iter;
  RLOCK();
  iwhmap_iter_init(_map_wsid_wsdata, &iter);
  while (iwhmap_iter_next(&iter)) {
    if (!visitor((void*) iter.val, data)) {
      break;
    }
  }
  UNLOCK();
  return 0;
}

iwrc grh_ws_peek_client_by_wsid(int wsid, ws_clients_visitor visitor, void *data) {
  RLOCK();
  struct ws_session *wss = iwhmap_get_u32(_map_wsid_wsdata, wsid);
  if (wss) {
    visitor(wss, data);
  }
  UNLOCK();
  return 0;
}

iwrc grh_ws_peek_client_by_session_uuid(const char *uuid, ws_clients_visitor visitor, void *data) {
  RLOCK();
  struct ws_session *wss = iwhmap_get(_map_uuid_sessions, uuid);
  if (wss) {
    visitor(wss, data);
  }
  UNLOCK();
  return 0;
}

iwrc grh_ws_send_all(const char *data, ssize_t len) {
  iwrc rc = 0;
  IWHMAP_ITER iter;
  if (len < 0) {
    len = strlen(data);
  }
  RLOCK();
  IWULIST idlist;
  rc = iwulist_init(&idlist, iwhmap_count(_map_wsid_wsdata), sizeof(int));
  if (rc) {
    UNLOCK();
    return rc;
  }
  iwhmap_iter_init(_map_wsid_wsdata, &iter);
  while (iwhmap_iter_next(&iter)) {
    int wsid = (int) (intptr_t) iter.key;
    iwulist_push(&idlist, &wsid);
  }
  UNLOCK();

  for (int i = 0, l = iwulist_length(&idlist); i < l; ++i) {
    int wsid = *(int*) iwulist_at2(&idlist, i);
    RLOCK();
    struct ws_session *wss = iwhmap_get_u32(_map_wsid_wsdata, wsid);
    if (wss) {
#ifdef _DEBUG
      int64_t user_id = grh_auth_get_userid(wss->ws->req);
      iwlog_debug("SEND[%d, %" PRId64 "]: %.*s", wss->wsid, user_id, (int) len, data);
#endif
      iwn_ws_server_write(wss->ws, data, len);
    }
    UNLOCK();
  }

  iwulist_destroy_keep(&idlist);
  return rc;
}

void grh_ws_send_by_wsid(int32_t wsid, const char *data, ssize_t len) {
  if (len < 0) {
    len = strlen(data);
  }
  RLOCK();
  struct ws_session *wss = iwhmap_get_u32(_map_wsid_wsdata, wsid);
  if (wss) {
#ifdef _DEBUG
    int64_t user_id = grh_auth_get_userid(wss->ws->req);
    iwlog_debug("SEND[%d, %" PRId64 "]: %.*s", wss->wsid, user_id, (int) len, data);
#endif
    iwn_ws_server_write(wss->ws, data, len);
  }
  UNLOCK();
}

void grh_ws_send_by_uuid(const char *uuid, const char *data, ssize_t len) {
  if (len < 0) {
    len = strlen(data);
  }
  RLOCK();
  struct ws_session *wss = iwhmap_get(_map_uuid_sessions, uuid);
  if (wss) {
#ifdef _DEBUG
    int64_t user_id = grh_auth_get_userid(wss->ws->req);
    iwlog_debug("SEND[%d, %" PRId64 "]: %.*s", wss->wsid, user_id, (int) len, data);
#endif
    iwn_ws_server_write(wss->ws, data, len);
  }
  UNLOCK();
}

iwrc grh_ws_send_all_room_participants(int64_t room_id, const char *data, ssize_t len) {
  JQL q = 0;
  iwrc rc = 0;
  EJDB_LIST qlist = 0;
  JBL_NODE n_events;
  IWHMAP_ITER iter;

  IWULIST wsidlist = { 0 };
  IWHMAP *idmap = iwhmap_create_u64(0);
  RCA(idmap, finish);

  RCC(rc, finish, iwulist_init(&wsidlist, 32, sizeof(uint32_t)));

  RCC(rc, finish, jql_create(&q, "rooms", "/= :? | /events"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, room_id));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &qlist));
  if (!qlist->first) {
    goto finish;
  }
  assert(qlist->pool && qlist->first->node);
  rc = jbn_at(qlist->first->node, "/events", &n_events);
  if (rc || n_events->type != JBV_ARRAY) {
    rc = 0;
    goto finish;
  }
  // See model.txt
  for (JBL_NODE n = n_events->child; n; n = n->next) {
    if (n->type != JBV_ARRAY) {
      continue;
    }
    JBL_NODE nn = n->child;
    if (!nn || nn->type != JBV_STR || !nn->next || strcmp("joined", nn->vptr) != 0) {
      continue;
    }
    nn = nn->next->next;
    if (!nn || nn->type != JBV_I64) {
      continue;
    }
    iwhmap_put_u64(idmap, nn->vi64, (void*) (intptr_t) -1); // Save user ID
  }
  if (iwhmap_count(idmap) == 0) {
    goto finish;
  }

  RLOCK();
  iwhmap_iter_init(_map_wsid_wsdata, &iter);
  while (iwhmap_iter_next(&iter)) {
    const struct ws_session *wss = iter.val;
    int64_t user_id = grh_auth_get_userid(wss->ws->req);
    if ((intptr_t) iwhmap_get(idmap, (void*) user_id) == (intptr_t) -1) {
      iwulist_push(&wsidlist, &wss->wsid);
    }
  }
  UNLOCK();

  for (int i = 0; i < wsidlist.num; ++i) {
    uint32_t wsid = *(uint32_t*) iwulist_at2(&wsidlist, i);
    RLOCK();
    struct ws_session *wss = iwhmap_get(_map_wsid_wsdata, (void*) (intptr_t) wsid);
    if (wss) {
#ifdef _DEBUG
      int64_t user_id = grh_auth_get_userid(wss->ws->req);
      iwlog_debug("SEND[%d, %" PRId64 "]: %.*s", wss->wsid, user_id, (int) len, data);
#endif
      iwn_ws_server_write(wss->ws, data, len);
    }
    UNLOCK();
  }

finish:
  ejdb_list_destroy(&qlist);
  jql_destroy(&q);
  iwhmap_destroy(idmap);
  iwulist_destroy_keep(&wsidlist);
  return rc;
}

iwrc grh_ws_send(struct ws_message_ctx *ctx, JBL_NODE json) {
  iwrc rc = 0;
  JBL_NODE n = 0;
  if (!json) {
    rc = RCR(jbn_from_json("{}", &json, ctx->pool));
  }
  if (ctx->hook && (json->type == JBV_OBJECT)) {
    jbn_at(json, "/hook", &n);
    if (n) {
      n->vptr = ctx->hook;
      n->child = 0;
      n->type = JBV_STR;
    } else {
      jbn_add_item_str(json, "hook", ctx->hook, -1, 0, ctx->pool);
    }
  }
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);
  RCC(rc, finish, jbn_as_json(json, jbl_xstr_json_printer, xstr, 0));
  grh_ws_send_by_wsid(ctx->wss->wsid, iwxstr_ptr(xstr), iwxstr_size(xstr));

finish:
  iwxstr_destroy(xstr);
  return rc;
}

static void _free_wsh_handlers_entry(void *k, void *v) {
  struct wsh_handler *h = v;
  while (h) {
    struct wsh_handler *n = h->next;
    if (h->dispose) {
      h->dispose(h->data);
    }
    free(h);
    h = n;
  }
  free(k);
}

iwrc grh_ws_send_confirm2(struct ws_message_ctx *ctx, JBL_NODE n, const char *error) {
  if (ctx->hook && error) {
    if (!jbn_from_json("{}", &n, ctx->pool)) { // Overwrite n in the case of error
      jbn_add_item_str(n, "error", error, -1, 0, ctx->pool);
    }
  }
  return ctx->hook ? grh_ws_send(ctx, n) : 0;
}

iwrc grh_ws_send_confirm(struct ws_message_ctx *ctx, const char *error) {
  return grh_ws_send_confirm2(ctx, 0, error);
}

iwrc grh_wss_add_event_listener(struct ws_session *wss, ws_event_listener_fn listener, void *data) {
  iwrc rc = 0;
  pthread_mutex_lock(&wss->ws->req->http->user_mtx);
  struct ws_event_listener_slot *s = wss->listeners;
  if (s) {
    if (s->listener == listener) {
      goto finish;
    }
    while (s->next) {
      s = s->next;
      if (s->listener == listener) {
        goto finish;
      }
    }
    struct ws_event_listener_slot *n = malloc(sizeof(*n));
    RCA(n, finish);
    *n = (struct ws_event_listener_slot) {
      .data = data,
      .listener = listener
    };
    s->next = n;
  } else {
    struct ws_event_listener_slot *n = malloc(sizeof(*n));
    RCA(n, finish);
    *n = (struct ws_event_listener_slot) {
      .data = data,
      .listener = listener
    };
    wss->listeners = n;
  }
finish:
  pthread_mutex_unlock(&wss->ws->req->http->user_mtx);
  return rc;
}

struct grh_user_data* grh_wss_get_data_of_type(struct ws_session *wss, int data_type) {
  return grh_req_data_find(wss->ws->req->http, data_type);
}

void grh_wss_set_data_of_type(struct ws_session *wss, struct grh_user_data *data) {
  grh_req_data_set(wss->ws->req->http, data);
}

void grh_wss_unset_data_of_type(struct ws_session *wss, int data_type) {
  grh_req_data_dispose(wss->ws->req->http, data_type);
}

iwrc grh_ws_register_wsh_handler(
  const char *cmd, const char *name, wsh_handler_fn wsh, void *data,
  void (*dispose)(void*)
  ) {
  if (!cmd || !name || !wsh) {
    return IW_ERROR_INVALID_ARGS;
  }
  RCR(_init());

  if (g_env.log.verbose) {
    iwlog_info("WSH: %s %s", name, cmd);
  }

  iwrc rc = 0;
  pthread_mutex_lock(&_handlers_mtx);

  char *ccmd = 0;
  struct wsh_handler *handler_new = 0;
  struct wsh_handler *handler = iwhmap_get(_wsh_handlers, cmd);

  RCB(finish, handler_new = malloc(sizeof(*handler_new)));
  handler_new->name = name;
  handler_new->wsh = wsh;
  handler_new->data = data;
  handler_new->dispose = dispose;
  handler_new->next = 0;

  if (handler) {
    while (handler->next) {
      handler = handler->next;
    }
    handler->next = handler_new;
  } else {
    RCB(finish, ccmd = strdup(cmd));
    RCC(rc, finish, iwhmap_put(_wsh_handlers, ccmd, handler_new));
  }

finish:
  pthread_mutex_unlock(&_handlers_mtx);
  if (rc) {
    _free_wsh_handlers_entry(ccmd, handler_new);
  }
  return rc; // NOLINT clang-analyzer-unix.Malloc
}

static iwrc _ping(struct ws_message_ctx *ctx, void *op) {
  return grh_ws_send_confirm(ctx, 0);
}

static bool _ws_message_handle(struct ws_session *wss, const char *msg, size_t msg_len) {
#ifdef _DEBUG
  {
    int64_t user_id = grh_auth_get_userid(wss->ws->req);
    iwlog_debug("RECV[%d, %" PRId64 "]: %.*s", wss->wsid, user_id, (int) msg_len, msg);
  }
#endif

  iwrc rc = 0;
  JBL_NODE n = 0, n2;
  const char *hook = 0, *cmd;
  bool locked = false;

  IWPOOL *pool = iwpool_create_empty();
  RCB(finish, pool);

  RCC(rc, finish, jbn_from_json(msg, &n, pool));
  jbn_at(n, "/cmd", &n2);
  if (n2 && (n2->type == JBV_STR)) {
    cmd = n2->vptr;
  } else {
    iwlog_warn("Invalid WS command received: %s", msg);
    rc = IW_ERROR_INVALID_VALUE;
    goto finish;
  }
  jbn_at(n, "/hook", &n2);
  if (n2 && (n2->type == JBV_STR)) {
    hook = n2->vptr;
  }
  pthread_mutex_lock(&_handlers_mtx), locked = true;
  struct wsh_handler *handler = iwhmap_get(_wsh_handlers, cmd);
  if (!handler) {
    iwlog_warn("Unknown command received: %s", msg);
    goto finish;
  }
  pthread_mutex_unlock(&_handlers_mtx), locked = false;

  struct ws_message_ctx ctx = {
    .cmd     = cmd,
    .hook    = hook,
    .payload = n,
    .wss     = wss,
    .pool    = pool
  };

  for ( ; handler; handler = handler->next) {
    rc = handler->wsh(&ctx, handler->data);
    if (rc) {
      iwlog_ecode_error(rc, "Handler %s failed on command %s", handler->name, cmd);
      goto finish;
    }
  }

finish:
  if (locked) {
    pthread_mutex_unlock(&_handlers_mtx);
  }
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc == 0;
}

static bool _ws_message_init(struct ws_session *wss, const char *msg, size_t msg_len) {
  if (msg_len != WS_TICKET_LEN) {
    return false;
  }
  iwrc rc = 0;
  char ticket[WS_TICKET_LEN + 1];
  memcpy(ticket, msg, msg_len);
  ticket[WS_TICKET_LEN] = '\0';

  if (!iwu_uuid_valid(ticket)) {
    return false;
  }

  RCC(rc, finish, _ws_ticket_pull(wss, ticket));
  iwn_ws_server_write(wss->ws, "{}", IW_LLEN("{}"));

  WLOCK();
  iwhmap_put_u32(_map_wsid_wsdata, wss->wsid, wss);
  iwhmap_put(_map_uuid_sessions, wss->uuid, wss);
  UNLOCK();

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  } else {
    wss->initialized = true;
  }
  return rc == 0;
}

static bool _on_ws_message(struct iwn_ws_sess *ws, const char *msg, size_t msg_len, uint8_t opcode) {
  struct ws_session *wss = (void*) grh_req_data_find(ws->req->http, GRH_USER_DATA_TYPE_WS);
  if (!wss) {
    return false;
  }
  if (IW_UNLIKELY(!wss->initialized)) {
    return _ws_message_init(wss, msg, msg_len);
  } else {
    return _ws_message_handle(wss, msg, msg_len);
  }
}

static iwrc _ws_ticket_save(const char *ticket, const char *session_id) {
  uint64_t ts;
  int64_t id;
  JBL jbl = 0;
  iwrc rc = RCR(iwp_current_time_ms(&ts, false));
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "name", ticket));
  RCC(rc, finish, jbl_set_string(jbl, "session_id", session_id));
  RCC(rc, finish, jbl_set_int64(jbl, "ts", ts));
  RCC(rc, finish, ejdb_put_new(g_env.db, "tickets", jbl, &id));

finish:
  jbl_destroy(&jbl);
  return rc;
}

static int _handler_ticket(struct iwn_wf_req *req, void *d) {
  iwrc rc = 0;
  int ret = 500;

  const char *sid = iwn_wf_session_id(req);
  if (!sid || !grh_auth_username(req)) {
    return 403;
  }

  char ticket[IW_UUID_STR_LEN + 1];
  iwu_uuid4_fill(ticket);
  ticket[IW_UUID_STR_LEN] = '\0';

  RCC(rc, finish, _ws_ticket_save(ticket, sid));
  RCC(rc, finish, iwn_http_response_header_set(req->http, "cache-control", "no-store, max-age=0", -1));
  iwn_http_response_write(req->http, 200, "text/plain", ticket, IW_UUID_STR_LEN);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return ret;
}

static void _on_wss_close(struct grh_user_data *ud) {
  struct ws_session *wss = (void*) ud;
  for (struct ws_event_listener_slot *s = wss->listeners; s; s = s->next) {
    if (s->listener) {
      s->listener(WS_EVENT_CLOSE, wss, s->data);
    }
  }
}

static void _on_wss_dispose(struct grh_user_data *ud) {
  struct ws_session *wss = (void*) ud;
  if (wss) {
    WLOCK();
    iwhmap_remove_u32(_map_wsid_wsdata, wss->wsid);
    UNLOCK();
  }
}

static void _on_wsid_wsdata_free(void *key, void *val) {
  struct ws_session *wss = val;
  if (val) {
    iwhmap_remove(_map_uuid_sessions, wss->uuid);
    for (struct ws_event_listener_slot *s = wss->listeners, *n = 0; s; s = n) {
      n = s->next;
      free(s);
    }
    free(wss);
  }
}

static bool _initialized = false;

void grh_ws_destroy(void) {
  if (!__sync_bool_compare_and_swap(&_initialized, true, false)) {
    grh_ws_user_destroy();
    iwhmap_destroy(_wsh_handlers);
    iwhmap_destroy(_map_wsid_wsdata);
    iwhmap_destroy(_map_uuid_sessions);
    pthread_rwlock_destroy(&_rwl);
    pthread_mutex_destroy(&_handlers_mtx);
  }
}

static iwrc _init(void) {
  if (!__sync_bool_compare_and_swap(&_initialized, false, true)) {
    return 0;  // initialized already
  }
  iwrc rc = 0;
  pthread_mutex_init(&_handlers_mtx, 0);
  pthread_rwlock_init(&_rwl, 0);

  RCB(finish, _map_wsid_wsdata = iwhmap_create_u32(_on_wsid_wsdata_free));
  RCB(finish, _map_uuid_sessions = iwhmap_create_str(0));
  RCB(finish, _wsh_handlers = iwhmap_create_str(_free_wsh_handlers_entry));

  RCC(rc, finish, grh_ws_register_wsh_handler("ping", "grh_ws::ping", _ping, 0, 0));
  RCC(rc, finish, grh_ws_user_init());

finish:
  if (rc) {
    grh_ws_destroy();
  }
  return rc;
}

static bool _on_wss_init(struct iwn_ws_sess *ws) {
  iwrc rc = 0;
  RCC(rc, finish, _init());

  struct ws_session *wss = calloc(1, sizeof(*wss));
  RCB(finish, wss);

  wss->type = GRH_USER_DATA_TYPE_WS;
  wss->close = _on_wss_close;
  wss->dispose = _on_wss_dispose;
  wss->wsid = ws->req->http->poller_adapter->fd;
  wss->ws = ws;

  iwu_uuid4_fill(wss->uuid);
  wss->uuid[IW_UUID_STR_LEN] = '\0';

  grh_req_data_add(ws->req->http, (void*) wss);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc == 0;
}

iwrc grh_route_ws(struct iwn_wf_route *parent) {
  iwrc rc = 0;

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/ws",
  }, &parent));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/ticket",
    .handler = _handler_ticket,
  }, 0));

  RCC(rc, finish, iwn_wf_route(iwn_ws_server_route_attach(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/channel",
    .tag = "ws"
  }, &(struct iwn_ws_handler_spec) {
    .handler = _on_ws_message,
    .on_session_init = _on_wss_init,
  }), 0));

finish:
  return rc;
}
