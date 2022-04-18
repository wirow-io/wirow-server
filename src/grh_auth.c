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

#include "grh_auth.h"
#include "gr_crypt.h"

#include "rct/rct.h"
#include "rct/rct_room.h"

#include <ejdb2/ejdb2.h>
#include <iowow/iwp.h>
#include <iowow/iwconv.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static const char *login_html
  = "<!DOCTYPE html>"
    "<html lang='en' data-status='b082c6'>"
    "<head>"
    "<title>Wirow</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1' />"
    "<link href='fonts.css' rel='stylesheet' type='text/css' />"
    "<link href='bundle.css' rel='stylesheet' type='text/css' />"
    "</head>"
    "<body>"
    "<script src='public.js'></script>"
    "</body>"
    "</html>";

static iwrc _authenticate(const char *username, const char *password, int64_t *out_id, char **out_perms) {
  *out_perms = 0;
  *out_id = 0;
  if (!username || !password) {
    return 0;
  }
  iwrc rc = 0;
  JQL q = 0;
  EJDB_LIST list = 0;
  EJDB db = g_env.db;

  char pwh[65];
  const char *upwh;
  const char *uperms = 0;
  int64_t salt = 0;

  RCC(rc, finish, jql_create(&q, "users", "/[name = :?] | /pwh + /salt + /perms"));
  RCC(rc, finish, jql_set_str(q, 0, 0, username));
  RCC(rc, finish, ejdb_list4(db, q, 1, 0, &list));

  if (!list->first) {
    goto finish;
  }
  RCC(rc, finish, jbl_object_get_i64(list->first->raw, "salt", &salt));
  RCC(rc, finish, jbl_object_get_str(list->first->raw, "pwh", &upwh));
  jbl_object_get_str(list->first->raw, "perms", &uperms);
  if (!salt || !upwh) {
    goto finish;
  }
  gr_crypt_pwhash(password, strlen(password), salt, pwh);

  if (strcmp(pwh, upwh) == 0) {
    *out_id = list->first->id;
    // Good! Now we re ready to give user permissions
    if (uperms) {
      *out_perms = strdup(uperms);
    } else {
      *out_perms = strdup("");
    }
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  ejdb_list_destroy(&list);
  jql_destroy(&q);
  return rc;
}

static int _handler_status(struct iwn_wf_req *req, void *data) {
  iwrc rc = 0;
  struct iwn_val val = iwn_pair_find_val(&req->query_params, "ref", IW_LLEN("ref"));
  if (val.len != IW_UUID_STR_LEN) {
    return 400;
  }

  JQL q = 0;
  JBL jbl = 0;
  EJDB_LIST list = 0;
  bool found = false;
  int ret = 500;
  const char *ref = val.buf;

  RCC(rc, finish, jbl_create_empty_object(&jbl));

  rct_lock();
  rct_room_t *room = rct_resource_by_uuid_unsafe(ref, RCT_TYPE_ROOM);
  if (room && !(room->flags & RCT_ROOM_PRIVATE)) {
    found = true;
    jbl_set_string(jbl, "name", room->name);
    jbl_set_bool(jbl, "active", true);
  }
  rct_unlock();

  if (!room) {
    RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?]"));
    RCC(rc, finish, jql_set_str(q, 0, 0, ref));
    RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));
    if (list->first) {
      int64_t flags;
      const char *name;
      RCC(rc, finish, jbl_object_get_str(list->first->raw, "name", &name));
      RCC(rc, finish, jbl_object_get_i64(list->first->raw, "flags", &flags));
      if (!(flags & RCT_ROOM_PRIVATE)) {
        found = true;
        jbl_set_string(jbl, "name", name);
        jbl_set_bool(jbl, "active", false);
      }
    }
  }

  if (!found) {
    jbl_set_bool(jbl, "active", false);
  }

  ret = iwn_http_response_jbl(req->http, jbl, 200, false);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  ejdb_list_destroy(&list);
  jbl_destroy(&jbl);
  jql_destroy(&q);
  return ret;
}

int64_t grh_auth_get_userid(struct iwn_wf_req *req) {
  if (req->http->user_id) {
    return req->http->user_id;
  }
  const char *buf = iwn_wf_session_get(req, GR_USER_ID_SESSION_KEY);
  req->http->user_id = buf ? iwatoi(buf) : 0;
  return req->http->user_id;
}

static int _guest_create(const char *ref, struct iwn_wf_req *req) {
  iwrc rc = 0;
  int64_t id;
  uint64_t ctime;

  JBL jbl = 0;
  bool room_found = false;
  char user[IW_UUID_STR_LEN + 1], *uuid;

  if (!ref || (strlen(ref) != IW_UUID_STR_LEN)) {
    goto finish;
  }

  iwu_uuid4_fill(user);
  user[IW_UUID_STR_LEN] = '\0';
  uuid = user;

  rct_lock();
  rct_room_t *room = rct_resource_by_uuid_unsafe(ref, RCT_TYPE_ROOM);
  room_found = room && !(room->flags & RCT_ROOM_PRIVATE);
  rct_unlock();

  if (!room_found) {
    goto finish;
  }

  RCC(rc, finish, iwp_current_time_ms(&ctime, false));
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "name", user));
  RCC(rc, finish, jbl_set_string(jbl, "uuid", uuid));
  RCC(rc, finish, jbl_set_int64(jbl, "ctime", ctime));
  RCC(rc, finish, jbl_set_bool(jbl, "guest", true));
  RCC(rc, finish, jbl_set_string(jbl, "perms", "guest"));

  RCC(rc, finish, ejdb_put_new(g_env.db, "users", jbl, &id));

  RCC(rc, finish, iwn_wf_session_put(req, GR_USER_SESSION_KEY, user));
  RCC(rc, finish, iwn_wf_session_printf(req, GR_USER_ID_SESSION_KEY, "%" PRId64, id));
  RCC(rc, finish, iwn_wf_session_put(req, GR_PERMISSIONS_SESSION_KEY, "guest"));

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jbl_destroy(&jbl);

  iwn_http_response_header_set(req->http, "location", "/", IW_LLEN("/"));
  return 302;
}

static bool _request_user_data_init(struct iwn_wf_req *req) {
  if (req->http->user_flags & REQ_FLAG_AUTH_OK) {
    return true;
  }
  if (iwn_wf_session_id(req) == 0) {
    return false;
  }
  const char *user = iwn_wf_session_get(req, GR_USER_SESSION_KEY);
  const char *uperms = iwn_wf_session_get(req, GR_PERMISSIONS_SESSION_KEY);
  if (!user || !uperms) {
    return false;
  }
  IWPOOL *pool = iwpool_create(64);
  if (!pool) {
    return false;
  }
  for (const char **s1 = iwpool_split_string(pool, uperms, ",", true); *s1; ++s1) {
    if (!(req->http->user_flags & REQ_FLAG_ROLE_ADMIN) && strcmp(*s1, "admin") == 0) {
      req->http->user_flags |= REQ_FLAG_ROLE_ADMIN;
    } else if (!(req->http->user_flags & REQ_FLAG_ROLE_USER) && strcmp(*s1, "user") == 0) {
      req->http->user_flags |= REQ_FLAG_ROLE_USER;
    }
  }
  iwpool_destroy(pool);
  req->http->user_flags |= REQ_FLAG_AUTH_OK;
  return true;
}

void grh_auth_request_init(struct iwn_wf_req *req) {
  req->http->user_flags &= ~(REQ_FLAG_ROLE_ALL | REQ_FLAG_AUTH_OK);
  _request_user_data_init(req);
}

static int _handler(struct iwn_wf_req *req, void *data) {
  // Check room status page
  if ((req->flags & IWN_WF_GET) && strcmp(req->path, "/status") == 0) {
    return _handler_status(req, 0);
  }

  bool auth_failed = false;
  bool on_login = strcmp(req->path, "/login.html") == 0;

  // Do not create session implicitly - DoS protection.
  const char *user = iwn_wf_session_get(req, GR_USER_SESSION_KEY);
  if (user && !on_login) {
    if (_request_user_data_init(req)) {
      return 0;
    }
  }

  if (on_login) {
    if (iwn_wf_session_id(req) != 0) {
      // /login.html will keep guest sessions.
      // Sessions for other type of users will be cleared.
      _request_user_data_init(req);
      if (req->http->user_flags & REQ_FLAG_ROLE_ALL) {
        iwn_wf_session_clear(req);
      }
    }
    if (req->flags & IWN_WF_POST) {
      int64_t id;
      char *perms;
      struct iwn_val user = iwn_pair_find_val(&req->form_params, "__ref__", IW_LLEN("__ref__"));
      if (user.len) {
        return _guest_create(user.buf, req);
      }
      user = iwn_pair_find_val(&req->form_params, "__username__", IW_LLEN("__username__"));
      struct iwn_val pw = iwn_pair_find_val(&req->form_params, "__password__", IW_LLEN("__password__"));
      iwrc rc = _authenticate(user.buf, pw.buf, &id, &perms);
      if (rc) {
        iwlog_ecode_error3(rc);
      }
      if (!perms || rc) {
        iwlog_info("Authentication failed: %s", user.buf);
        auth_failed = true;
        goto login;
      }
      iwlog_info("Authentication success: %s", user.buf);

      iwn_wf_session_put(req, GR_USER_SESSION_KEY, user.buf);
      iwn_wf_session_printf(req, GR_USER_ID_SESSION_KEY, "%" PRId64, id);
      iwn_wf_session_put(req, GR_PERMISSIONS_SESSION_KEY, perms);
      free(perms);

      iwn_http_response_header_set(req->http, "location", "/", IW_LLEN("/"));
      return 302;
    }
  }

login:
  if (!on_login) {
    struct iwn_val val = iwn_http_request_header_get(req->http, "connection", IW_LLEN("connection"));
    bool is_upgrade = strncmp(val.buf, "upgrade", val.len) == 0;
    if (is_upgrade) {
      return 0;
    } else {
      iwn_http_response_header_set(req->http, "location", "/login.html", IW_LLEN("/login.html"));
      return 302;
    }
  } else {
    char *html = strdup(login_html);
    if (!html) {
      return 500;
    }
    if (auth_failed) {
      char *status = strstr(html, "b082c6");
      if (status) { // patch status placeholder
        memcpy(status, "failed", sizeof("failed") - 1);
      }
    }
    int ret = iwn_http_response_write(req->http, 200, "text/html; charset=utf8", html, -1) ? 1 : -1;
    free(html);
    return ret;
  }
}

const char* grh_auth_username(struct iwn_wf_req *req) {
  return iwn_wf_session_get(req, GR_USER_SESSION_KEY);
}

bool grh_auth_has_any_perms(struct iwn_wf_req *req, const char *perms) {
  if (!perms || (*perms == '\0')) {
    return false;
  }
  if (strcmp(perms, "admin") == 0) {
    return (req->http->user_flags & REQ_FLAG_ROLE_ADMIN);
  }
  if (strcmp(perms, "user") == 0) {
    return (req->http->user_flags & REQ_FLAG_ROLE_USER);
  }

  const char *uperms = iwn_wf_session_get(req, GR_PERMISSIONS_SESSION_KEY);
  if (!uperms) {
    return false;
  }
  IWPOOL *pool = iwpool_create(64);
  if (!pool) {
    return false;
  }
  for (const char **s1 = iwpool_split_string(pool, uperms, ",", true); *s1; ++s1) {
    for (const char **s2 = iwpool_split_string(pool, perms, ",", true); *s2; ++s2) {
      if (strcmp(*s1, *s2) == 0) {
        iwpool_destroy(pool);
        return true;
      }
    }
  }
  iwpool_destroy(pool);
  return false;
}

bool grh_auth_is_room_member(const char *room_cid, int64_t user_id, bool *out_room_owner) {
  if (!room_cid) {
    return false;
  }
  JQL q;
  bool ret = false;
  *out_room_owner = false;
  EJDB_LIST list = 0;
  char key[IW_UUID_STR_LEN + NUMBUSZ + 2];
  snprintf(key, sizeof(key), "%" PRId64 ":%s", user_id, room_cid);
  iwrc rc = RCR(jql_create(&q, "joins", "/[k = :?]"));
  RCC(rc, finish, jql_set_str(q, 0, 0, key));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));
  if (list->first) {
    ret = true;
    RCC(rc, finish, jbl_object_get_bool(list->first->raw, "o", out_room_owner));
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  return ret;
}

iwrc grh_auth_routes(const struct iwn_wf_route *parent, struct iwn_wf_route **out) {
  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .handler = _handler,
    .flags = IWN_WF_METHODS_ALL,
    .tag = "auth"
  }, out);
}
