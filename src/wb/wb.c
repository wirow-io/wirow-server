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

#include "wb.h"
#include "grh.h"
#include "rct/rct.h"
#include "lic_env.h"
#include "grh_auth.h"

#include <ejdb2/ejdb2.h>
#include <iowow/iwpool.h>
#include <iowow/iwuuid.h>
#include <iowow/iwxstr.h>
#include <iowow/iwhmap.h>

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#define ROOM_CID_REGEX "[a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12}"

static pthread_mutex_t _mtx;
static struct wb_room_t *_rooms = 0;

struct wb_room_t {
  size_t version, version_init; // Increments when someone opens room
  size_t refs;
  struct wb_room_t *next;
  char    cid[IW_UUID_STR_LEN + 1];
  int64_t ejdb_id;

  pthread_mutex_t state_mtx;
  IWHMAP     *elements;
  atomic_bool loaded;

  struct wb_user_t *users;
  size_t next_user_id;
};

struct wb_user_t {
  struct wb_room_t   *room;
  struct wb_user_t   *next;
  struct iwn_ws_sess *ws;
  char     id[sizeof(size_t) * 2 + 1]; // size_t in hex format
  bool     readonly;
  IWPOOL  *pool;
  JBL_NODE state;
};

struct wb_element_t {
  IWPOOL  *pool;
  JBL_NODE state;
};

static void _wb_room_load(void *_room);
static void _wb_room_save(void *_room);
static iwrc _wb_room_close(struct wb_room_t *room);

static iwrc _wb_element_create(struct wb_element_t **el_out) {
  iwrc rc = 0;
  struct wb_element_t *el = 0;

  RCA((el = malloc(sizeof(*el))), finish);
  RCA((el->pool = iwpool_create_empty()), finish);
  RCA((el->state = iwpool_calloc(sizeof(*el->state), el->pool)), finish);
  el->state->type = JBV_OBJECT;

finish:
  if (rc) {
    iwpool_destroy(el ? el->pool : 0);
    free(el);
  }
  *el_out = rc ? 0 : el;
  return rc;
}

static void _wb_element_destroy(struct wb_element_t *el) {
  if (el) {
    iwpool_destroy(el->pool);
    free(el);
  }
}

static void _wb_elements_kv_free(void *key, void *value) {
  _wb_element_destroy((struct wb_element_t*) value);
  free(key);
}

// Returns true if new is selected for use
static bool _wb_elements_compare_versions_jbn(JBL_NODE old_el, JBL_NODE new_el) {
  int64_t old_ver, old_nonce, new_ver, new_nonce;
  JBL_NODE n;

  if (jbn_at(old_el, "/version", &n) || n->type != JBV_I64) {
    return false;
  }
  old_ver = n->vi64;

  if (jbn_at(old_el, "/versionNonce", &n) || n->type != JBV_I64) {
    return false;
  }
  old_nonce = n->vi64;

  if (jbn_at(new_el, "/version", &n) || n->type != JBV_I64) {
    return false;
  }
  new_ver = n->vi64;

  if (jbn_at(new_el, "/versionNonce", &n) || n->type != JBV_I64) {
    return false;
  }
  new_nonce = n->vi64;

  // Versions are incremented on changes and nonce is random. Selection is deterministic
  return new_ver > old_ver || (new_ver == old_ver && new_nonce > old_nonce);
}

static iwrc _wb_room_create(struct wb_room_t **room_out, const char *cid) {
  iwrc rc = 0;
  struct wb_room_t *room;
  pthread_mutexattr_t state_mtx_attr;

  RCA((room = calloc(1, sizeof(*_rooms))), finish);

  memcpy(room->cid, cid, IW_UUID_STR_LEN);
  room->cid[IW_UUID_STR_LEN] = '\0';

  RCA((room->elements = iwhmap_create_str(_wb_elements_kv_free)), finish);

  pthread_mutexattr_init(&state_mtx_attr);
  pthread_mutexattr_settype(&state_mtx_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&room->state_mtx, &state_mtx_attr);

finish:
  pthread_mutexattr_destroy(&state_mtx_attr);
  if (rc && room) {
    iwhmap_destroy(room->elements);
    free(room);
  }
  *room_out = rc ? 0 : room;
  return rc;
}

static void _wb_room_destroy(struct wb_room_t *room) {
  if (room) {
    pthread_mutex_destroy(&room->state_mtx);
    iwhmap_destroy(room->elements);
    free(room);
  }
}

static iwrc _wb_room_open(struct wb_room_t **room_out, const char *cid, bool readonly) {
  iwrc rc = 0;
  struct wb_room_t *room = 0;

  pthread_mutex_lock(&_mtx);

  for (struct wb_room_t *i = _rooms; i; i = i->next) {
    if (strcmp(cid, i->cid) == 0) {
      room = i;
      break;
    }
  }

  if (!room) {
    RCC(rc, finish, _wb_room_create(&room, cid));

    room->refs = 1; // 1 for load task
    room->next = _rooms;
    _rooms = room;
    RCC(rc, finish, iwtp_schedule(g_env.tp, _wb_room_load, room));
  }

  room->refs++;
  if (!readonly) {
    room->version++;
  }

finish:
  if (rc && room) {
    // Close loading reference and destroy room
    _wb_room_close(room);
  }
  pthread_mutex_unlock(&_mtx);
  *room_out = rc ? 0 : room;
  return rc;
}

static iwrc _wb_room_close(struct wb_room_t *room) {
  if (!room) {
    return 0;
  }
  iwrc rc = 0;
  bool destroy = false;

  pthread_mutex_lock(&_mtx);
  room->refs--;

  if (room->refs == 0 && room->loaded) {
    RCC(rc, finish, iwtp_schedule(g_env.tp, _wb_room_save, room));
    room->refs++;
  }

  if (room->refs == 0 && !room->loaded) {
    if (room == _rooms) {
      _rooms = room->next;
    } else {
      for (struct wb_room_t *i = _rooms; i && i->next; i = i->next) {
        if (room == i->next) {
          i->next = room->next;
          break;
        }
      }
    }

    destroy = true;
  }

  pthread_mutex_unlock(&_mtx);

finish:
  if (rc) {
    iwlog_ecode_error(rc, "Whiteboard room closing error");
  }
  if (destroy) {
    _wb_room_destroy(room);
  }
  return rc;
}

static iwrc _wb_room_sync_jbn(struct wb_room_t *room, JBL_NODE *node_out, IWPOOL *pool, struct wb_user_t *ignore_user) {
  iwrc rc = 0;
  JBL_NODE n, n2, n3;
  IWHMAP_ITER iter_el;

  pthread_mutex_lock(&room->state_mtx);

  RCC(rc, finish, jbn_from_json("{\"sync\":{\"e\":{},\"c\":{}}}", node_out, pool));

  if (!jbn_at(*node_out, "/sync/e", &n) && n->type == JBV_OBJECT) {
    iwhmap_iter_init(room->elements, &iter_el);
    while (iwhmap_iter_next(&iter_el)) {
      RCC(rc, finish, jbn_add_item_obj(n, (const char*) iter_el.key, &n2, pool));
      RCC(rc, finish, jbn_clone(((struct wb_element_t*) iter_el.val)->state, &n3, pool));
      jbn_apply_from(n2, n3);
    }
  } else {
    RCC(rc, finish, IW_ERROR_INVALID_STATE);
  }

  if (!jbn_at(*node_out, "/sync/c", &n) && n->type == JBV_OBJECT) {
    for (struct wb_user_t *i = room->users; i; i = i->next) {
      // Collect users to list without readonly users (to hide them)
      if (i != ignore_user && !i->readonly) {
        RCC(rc, finish, jbn_add_item_obj(n, i->id, &n2, pool));
        RCC(rc, finish, jbn_clone(i->state, &n3, pool));
        jbn_apply_from(n2, n3);
      }
    }
  } else {
    RCC(rc, finish, IW_ERROR_INVALID_STATE);
  }

finish:
  pthread_mutex_unlock(&room->state_mtx);
  return rc;
}

static void _wb_room_load(void *_room) {
  iwrc rc = 0;
  struct wb_room_t *room = 0;
  IWPOOL *pool;
  JBL_NODE json, n, n2, n3;
  bool locked = false, locked2 = false;
  JQL q = 0;
  EJDB_DOC doc;
  struct wb_element_t *el = 0;
  char *el_key = 0;

  RCA((pool = iwpool_create_empty()), finish);

  RCA((room = _room), finish);

  // Maybe there is no users so can skip loading and destroy room
  pthread_mutex_lock(&_mtx), locked2 = true;
  if (room->refs == 1) {
    RCC(rc, finish, _wb_room_close(room));
    room = 0;
    goto finish;
  }
  pthread_mutex_unlock(&_mtx), locked2 = false;

  RCC(rc, finish, jql_create(&q, "whiteboards", "/[uuid = :?] or /[cid = :?] | /* | limit 1"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room->cid));
  RCC(rc, finish, jql_set_str(q, 0, 1, room->cid));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 1, pool));

  // No one will try to modify state when room->loaded == false
  // But new users can connect so do not lock mutex here
  if (doc) {
    room->ejdb_id = doc->id;
    RCC(rc, finish, jbn_at(doc->node, "/elements", &json));
    for (n = json->child; n; n = n->next) {
      RCC(rc, finish, _wb_element_create(&el));
      RCC(rc, finish, jbn_clone(n, &n2, el->pool));
      jbn_apply_from(el->state, n2);

      RCA((el_key = malloc(sizeof(*n->key) * (n->klidx + 1))), finish);
      el_key[n->klidx] = '\0';
      strncpy(el_key, n->key, n->klidx);

      RCC(rc, finish, iwhmap_put(room->elements, el_key, el));
      el = 0, el_key = 0;
    }
  }

  pthread_mutex_lock(&room->state_mtx), locked = true;

  room->loaded = true;

  RCC(rc, finish, _wb_room_sync_jbn(room, &json, pool, 0));
  RCC(rc, finish, jbn_add_item_bool(json, "readonly", false, &n3, pool));
  RCC(rc, finish, jbn_at(json, "/sync/c", &n));

  char id[sizeof(room->users->id) / sizeof(*room->users->id) + 1] = "/";
  for (struct wb_user_t *u = room->users; u; u = u->next) {
    n3->vbool = u->readonly;
    if (u->readonly) {
      // Readonly users are not included in sync packet so dont need to hide object
      iwn_ws_server_write_jbn(u->ws, json);
    } else {
      // Send sync packet to each user without that users
      memcpy(&id[1], u->id, sizeof(u->id));
      RCC(rc, finish, jbn_at(n, id, &n2));
      jbn_remove_item(n, n2);
      iwn_ws_server_write_jbn(u->ws, json);
      jbn_add_item(n, n2);
    }
  }

  pthread_mutex_unlock(&room->state_mtx), locked = false;

finish:
  if (rc) {
    iwlog_ecode_debug(rc, "Whiteboard room loading error");
  }
  if (locked) {
    pthread_mutex_unlock(&room->state_mtx);
  }
  if (locked2) {
    pthread_mutex_unlock(&_mtx);
  }
  jql_destroy(&q);
  _wb_element_destroy(el);
  free(el_key);
  iwpool_destroy(pool);
  _wb_room_close(room);
}

static void _wb_room_save(void *_room) {
  iwrc rc = 0;
  struct wb_room_t *room = 0;
  IWPOOL *pool = 0;
  JBL_NODE json, n, n2;
  JBL jbl = 0;
  bool locked = false;
  uint64_t ts;
  size_t ver = 0;

  RCA((pool = iwpool_create_empty()), finish);

  RCA((room = _room), finish);
  // If there are users
  pthread_mutex_lock(&_mtx), locked = true;
  if (room->refs != 1) {
    RCC(rc, finish, _wb_room_close(room));
    room = 0;
    goto finish;
  }
  ver = room->version;
  // If room was not changed since start
  if (ver == room->version_init) {
    room->loaded = false;
    RCC(rc, finish, _wb_room_close(room));
    room = 0;
    goto finish;
  }
  pthread_mutex_unlock(&_mtx), locked = false;

  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  RCC(rc, finish, jbn_from_json_printf(&json, pool, "{\"ctime\": %" PRIu64 ", \"elements\": {}}", ts));
  RCC(rc, finish, jbn_add_item_str(json, "cid", room->cid, -1, 0, pool));

  RCC(rc, finish, _wb_room_sync_jbn(room, &n, pool, 0));
  RCC(rc, finish, jbn_at(n, "/sync/e", &n2));

  RCC(rc, finish, jbn_at(json, "/elements", &n));
  jbn_apply_from(n, n2);

  RCC(rc, finish, jbl_from_node(&jbl, json));
  if (room->ejdb_id) {
    RCC(rc, finish, ejdb_put(g_env.db, "whiteboards", jbl, room->ejdb_id));
  } else {
    RCC(rc, finish, ejdb_put_new_jbn(g_env.db, "whiteboards", json, 0));
  }

  pthread_mutex_lock(&_mtx), locked = true;
  // Ensure that saved data is relevant
  // If it is then destroy room immediately
  if (room->version == ver) {
    room->loaded = false;
    RCC(rc, finish, _wb_room_close(room));
    room = 0;
    goto finish;
  }
  pthread_mutex_unlock(&_mtx), locked = false;

finish:
  if (locked) {
    pthread_mutex_unlock(&_mtx);
  }
  if (rc) {
    iwlog_ecode_debug(rc, "Whiteboard room loading error");
  }
  jbl_destroy(&jbl);
  iwpool_destroy(pool);
  _wb_room_close(room);
}

static iwrc _wb_room_commit_element(struct wb_room_t *room, JBL_NODE patch, JBL_NODE *rollback_out, IWPOOL *pool) {
  iwrc rc = 0;
  JBL_NODE n, rollback = 0;
  IWPOOL *pool2 = 0;
  struct wb_element_t *json_el;
  char *json_el_key = 0;

  if (!jbn_at(patch, "/id", &n)) {
    // Excalidraw has id as internal field
    // Do not store it because it is object key
    jbn_remove_item(patch, n);
  }

  RCA((json_el_key = malloc(sizeof(*patch->key) * (patch->klidx + 1))), finish);
  json_el_key[patch->klidx] = '\0';
  strncpy(json_el_key, patch->key, patch->klidx);

  if ((json_el = iwhmap_get(room->elements, json_el_key))) {
    if (_wb_elements_compare_versions_jbn(json_el->state, patch)) {
      RCC(rc, finish, jbn_merge_patch(json_el->state, patch, json_el->pool));
      RCA((pool2 = iwpool_create_empty()), finish);
      RCC(rc, finish, jbn_clone(json_el->state, &n, pool2));
      iwpool_destroy(json_el->pool);
      json_el->pool = pool2, json_el->state = n;
    } else {
      RCC(rc, finish, jbn_clone(json_el->state, &rollback, pool));
    }
  } else {
    RCC(rc, finish, _wb_element_create(&json_el));
    RCC(rc, finish, jbn_clone(patch, &json_el->state, json_el->pool));
    RCC(rc, finish, iwhmap_put(room->elements, json_el_key, json_el));
    json_el_key = 0; // Need to keep it for iwhmap
  }

finish:
  free(json_el_key);
  if (rc) {
    iwpool_destroy(pool);
  }
  *rollback_out = rollback;
  return rc;
}

static iwrc _wb_user_create(
  struct wb_user_t  **user_out,
  struct wb_room_t   *room,
  struct iwn_ws_sess *ws,
  const char         *username,
  bool                readonly
  ) {
  iwrc rc = 0;
  struct wb_user_t *user = 0;

  RCA((user = malloc(sizeof(*room->users))), finish);
  RCA((user->pool = iwpool_create_empty()), finish);

  user->ws = ws;
  user->room = room;
  user->readonly = readonly;

  RCA((user->state = iwpool_calloc(sizeof(*user->state), user->pool)), finish);
  user->state->type = JBV_OBJECT;
  if (username) {
    RCC(rc, finish, jbn_add_item_str(user->state, "username", username, -1, 0, user->pool));
  }

  pthread_mutex_lock(&room->state_mtx);
  snprintf(user->id, sizeof(user->id) / sizeof(*user->id), "%lx", ++room->next_user_id);
  user->next = room->users;
  room->users = user;
  pthread_mutex_unlock(&room->state_mtx);

finish:
  if (rc && user) {
    iwpool_destroy(user->pool);
    free(user);
  }
  *user_out = rc ? 0 : user;
  return rc;
}

static void _wb_user_destroy(struct wb_user_t *user) {
  if (user) {
    struct wb_room_t *room = user->room;

    pthread_mutex_lock(&room->state_mtx);
    if (user == room->users) {
      room->users = user->next;
    } else {
      for (struct wb_user_t *i = room->users; i && i->next; i = i->next) {
        if (user == i->next) {
          i->next = user->next;
          break;
        }
      }
    }
    pthread_mutex_unlock(&room->state_mtx);

    iwpool_destroy(user->pool);
    free(user);
  }
}

// When room = 0, send to user, otherwise send to all room participants except user
static iwrc _wb_ws_send(const char *data, size_t data_len, struct wb_room_t *room, struct wb_user_t *user) {
  iwrc rc = 0;
  if (room) {
    pthread_mutex_lock(&room->state_mtx);
    for (struct wb_user_t *u = room->users; u; u = u->next) {
      if (u != user) {
        iwn_ws_server_write(u->ws, data, data_len);
      }
    }
    pthread_mutex_unlock(&room->state_mtx);
  } else if (user) {
    iwn_ws_server_write(user->ws, data, data_len);
  } else {
    RCC(rc, finish, IW_ERROR_INVALID_ARGS);
  }
finish:
  return rc;
}

static iwrc _wb_ws_send_jbn(JBL_NODE data, struct wb_room_t *room, struct wb_user_t *user) {
  iwrc rc = 0;
  IWXSTR *xstr;

  RCA((xstr = iwxstr_new()), finish);
  RCC(rc, finish, jbn_as_json(data, jbl_xstr_json_printer, xstr, 0));
  RCC(rc, finish, _wb_ws_send(iwxstr_ptr(xstr), iwxstr_size(xstr), room, user));

finish:
  iwxstr_destroy(xstr);
  return rc;
}

static void _wb_ws_destroy_userdata(struct grh_user_data *u) {
  struct wb_user_t *user = u->data;
  struct wb_room_t *room = user->room;
  bool send_leave = !user->readonly;

  const char fmt[] = "{\"c\":{\"%s\":{\"isDeleted\":true}}}";
  char msg[sizeof(fmt) / sizeof(*fmt) + sizeof(user->id) / sizeof(*user->id) + 1];
  int msg_len = snprintf(msg, sizeof(msg) / sizeof(*msg), fmt, user->id);

  _wb_user_destroy(user);
  if (send_leave) {
    _wb_ws_send(msg, msg_len, room, 0);
  }
  _wb_room_close(room);
  free(u);
}

static bool _on_ws_message(struct iwn_ws_sess *ws, const char *msg, size_t msg_len) {
  iwrc rc = 0;
  JBL_NODE json_in, json_out, json_out_back, n, n2, n3;
  struct wb_element_t *json_el = 0;
  char *json_el_key = 0;
  IWPOOL *pool = 0, *pool2 = 0;
  bool locked = false, ret = false;
  struct wb_room_t *room;
  struct grh_user_data *udata = grh_req_data_find(ws->req->http, GRH_USER_DATA_TYPE_WB);
  struct wb_user_t *user = udata ? udata->data : 0;

  if (!user || !user->room || !user->room->loaded) {
    return false;
  }
  room = user->room;

  RCA(pool = iwpool_create_empty(), finish);
  if ((rc = jbn_from_json(msg, &json_in, pool))) {
    RCC(rc, finish, IW_ERROR_INVALID_VALUE);
  }

  RCA((json_out = iwpool_calloc(sizeof(*json_out), pool)), finish);
  json_out->type = JBV_OBJECT;
  RCA((json_out_back = iwpool_calloc(sizeof(*json_out_back), pool)), finish);
  json_out_back->type = JBV_OBJECT;

  if (!jbn_at(json_in, "/ping", &n) && n->type == JBV_BOOL && n->vbool == 1) {
    RCC(rc, finish, jbn_add_item_bool(json_out_back, "pong", true, 0, pool));
  }

  if (!user->readonly) {
    pthread_mutex_lock(&room->state_mtx), locked = true;

    if (!jbn_at(json_in, "/e", &n) && n->type == JBV_OBJECT) {
      JBL_NODE n_els = 0, n_els_back = 0;

      RCC(rc, finish, jbn_add_item_obj(json_out, "e", &n_els, pool));
      RCC(rc, finish, jbn_add_item_obj(json_out_back, "e", &n_els_back, pool));

      for (n = n->child; n; n = n->next) {
        RCC(rc, finish, _wb_room_commit_element(room, n, &n2, pool));
        if (!n2) { // Rollback is zero means that update wal applied, broadcasting it
          RCC(rc, finish, jbn_add_item_obj(n_els, n->key, &n3, pool));
          jbn_apply_from(n3, n);
        } else { // Received update was not applied, sending current state back
          RCC(rc, finish, jbn_add_item_obj(n_els_back, n->key, &n3, pool));
          jbn_apply_from(n3, n2);
        }
      }

      if (!n_els->child) {
        jbn_remove_item(json_out, n_els);
      }
      if (!n_els_back->child) {
        jbn_remove_item(json_out_back, n_els_back);
      }
    }

    if (!jbn_at(json_in, "/s", &n) && n->type == JBV_OBJECT) {
      RCC(rc, finish, jbn_clone(n, &n2, user->pool));
      RCC(rc, finish, jbn_merge_patch(user->state, n2, user->pool));
      RCA((pool2 = iwpool_create_empty()), finish);
      RCC(rc, finish, jbn_clone(user->state, &n3, pool2));
      iwpool_destroy(user->pool);
      user->pool = pool2, user->state = n3;

      RCC(rc, finish, jbn_add_item_obj(json_out, "c", &n2, pool));
      RCC(rc, finish, jbn_add_item_obj(n2, user->id, &n3, pool));
      jbn_apply_from(n3, n);
    }

    pthread_mutex_unlock(&room->state_mtx), locked = false;
  }

  if (json_out_back->child) {
    RCC(rc, finish, _wb_ws_send_jbn(json_out_back, 0, user));
  }
  if (json_out->child) {
    RCC(rc, finish, _wb_ws_send_jbn(json_out, room, user));
  }

  ret = true;

finish:
  if (locked) {
    pthread_mutex_unlock(&room->state_mtx);
  }
  if (rc) {
    iwlog_ecode_error(rc, "Whiteboard msg handler error");
    if (json_el) {
      iwpool_destroy(json_el->pool);
      free(json_el);
    }
    if (json_el_key) {
      free(json_el_key);
    }
    iwpool_destroy(pool2);
  }
  iwpool_destroy(pool);
  return ret;
}

// If error occurs, room will be "webinar" to restrict maximum access
static bool _rct_is_webinar(const char *room_cid) {
  if (!room_cid) {
    return true;
  }
  iwrc rc = 0;
  JQL q = 0;
  bool ret = true;
  EJDB_LIST list = 0;
  JBL_NODE n = 0;

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] or /[cid = :?] | /flags | limit 1"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room_cid));
  RCC(rc, finish, jql_set_str(q, 0, 1, room_cid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));
  if (list->first) {
    RCC(rc, finish, jbn_at(list->first->node, "/flags", &n));
    ret = n->vi64 & RCT_ROOM_WEBINAR;
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  return ret;
}

static iwrc _rct_get_title(const char *room_cid, const char **title, IWPOOL *pool) {
  if (!room_cid) {
    *title = 0;
    return 0;
  }
  iwrc rc = 0;
  JQL q = 0;
  JBL_NODE n = 0;
  EJDB_DOC doc;

  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] or /[cid = :?] | /name | limit 1"));
  RCC(rc, finish, jql_set_str(q, 0, 0, room_cid));
  RCC(rc, finish, jql_set_str(q, 0, 1, room_cid));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 1, pool));
  if (doc && !jbn_at(doc->node, "/name", &n) && n->type == JBV_STR) {
    *title = n->vptr;
  } else {
    *title = 0;
  }

finish:
  if (rc) {
    *title = 0;
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  return rc;
}

static bool _on_wss_init(struct iwn_ws_sess *ws) {
  iwrc rc = 0;
  bool ret = false;
  struct iwn_wf_req *req = ws->req;
  struct iwn_wf_route_submatch *sm = iwn_wf_request_submatch_last(req);
  if (!sm || sm->ep - sm->sp != IW_UUID_STR_LEN) {
    return false;
  }
  char room_cid[IW_UUID_STR_LEN + 1];
  memcpy(room_cid, sm->sp, IW_UUID_STR_LEN);
  room_cid[IW_UUID_STR_LEN] = '\0';

  struct iwn_val username = iwn_pair_find_val(&req->query_params, "username", IW_LLEN("username"));
  struct wb_room_t *room = 0;
  struct wb_user_t *user = 0;
  const char *room_title = 0;
  JBL_NODE json, n, n1;
  int64_t user_id;
  bool is_participant, is_room_owner, is_readonly;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  user_id = grh_auth_get_userid(req);
  is_participant = user_id ? grh_auth_is_room_member(room_cid, user_id, &is_room_owner) : false;
  is_readonly = !is_participant || !username.len || (_rct_is_webinar(room_cid) && !is_room_owner);
  RCC(rc, finish, _rct_get_title(room_cid, &room_title, pool));

  if ((!g_env.whiteboard.public_available && !is_participant) || !room_title) {
    goto finish;
  }

  RCC(rc, finish, _wb_room_open(&room, room_cid, is_readonly));
  RCC(rc, finish, _wb_user_create(&user, room, ws, username.buf, is_readonly));

  {
    struct grh_user_data *d = malloc(sizeof(*d));
    RCB(finish, d);
    *d = (struct grh_user_data) {
      .type = GRH_USER_DATA_TYPE_WB,
      .data = user,
      .dispose = _wb_ws_destroy_userdata
    };
    grh_req_data_set(req->http, d);
  }

  if (room->loaded && !user->readonly) { // Broadcast new user
    RCC(rc, finish, jbn_from_json("{\"c\": {}}", &json, pool));
    RCC(rc, finish, jbn_at(json, "/c", &n));
    RCC(rc, finish, jbn_add_item_obj(n, user->id, &n1, pool));
    jbn_apply_from(n1, user->state);
    RCC(rc, finish, _wb_ws_send_jbn(json, room, user));
  }

  // Send room title
  RCC(rc, finish, jbn_from_json("{\"title\": \"\"}", &json, pool));
  if (!jbn_at(json, "/title", &n1) && n1->type == JBV_STR) {
    n1->vptr = room_title;
    n1->vsize = (int) strlen(room_title);
  }
  RCC(rc, finish, _wb_ws_send_jbn(json, 0, user));

  if (room->loaded) {
    RCC(rc, finish, _wb_room_sync_jbn(room, &json, pool, user));
    RCC(rc, finish, jbn_add_item_bool(json, "readonly", is_readonly, 0, pool));
    RCC(rc, finish, _wb_ws_send_jbn(json, 0, user));
  }

  ret = true;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  iwpool_destroy(pool);
  return ret;
}

static void _on_handler_dispose(struct iwn_wf_ctx *ctx, void *user_data) {
  pthread_mutex_destroy(&_mtx);
}

iwrc grh_route_wb(struct iwn_wf_route *parent) {
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&_mtx, &mattr);
  pthread_mutexattr_destroy(&mattr);

  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/whiteboard"
  }, &parent));

  RCR(iwn_wf_route(iwn_ws_server_route_attach(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "^/ws/(" ROOM_CID_REGEX ")",
    .tag = "ws"
  }, &(struct iwn_ws_handler_spec) {
    .handler = _on_ws_message,
    .on_session_init = _on_wss_init,
    .on_handler_dispose = _on_handler_dispose,
  }), 0));
  return 0;
}
