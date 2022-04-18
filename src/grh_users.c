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

#include "grh_users.h"
#include "grh_auth.h"

#include <ejdb2/ejdb2.h>
#include <iowow/iwpool.h>

static iwrc _init_fill(struct iwn_wf_req *req, JBL_NODE *out, IWPOOL *pool) {
  iwrc rc = 0;
  JQL q = 0;
  EJDB_DOC first = 0;
  JBL_NODE n;

  const char *username = grh_auth_username(req);
  if (!username) {
    iwlog_error("No username in session: %s", iwn_wf_session_id(req));
    return IW_ERROR_INVALID_STATE;
  }

  RCC(rc, finish, jql_create(&q, "users", "/[name = :?] | /{name,perms,rooms}"));
  RCC(rc, finish, jql_set_str(q, 0, 0, username));
  RCC(rc, finish, ejdb_list(g_env.db, q, &first, 1, pool));

  if (!first) {
    iwlog_error("User %s is not found", username);
    rc = IW_ERROR_NOT_EXISTS;
    goto finish;
  }
  RCC(rc, finish, jbn_add_item_obj(first->node, "system", &n, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "alo_threshold", g_env.alo.threshold, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "alo_interval", g_env.alo.interval_ms, 0, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "max_upload_size", g_env.uploads.max_size, 0, pool));

  *out = first->node;

finish:
  jql_destroy(&q);
  return rc;
}

static int _init(struct iwn_wf_req *req, void *data) {
  iwrc rc = 0;
  int ret = 500;
  JBL_NODE n;
  IWPOOL *pool;

  RCA(pool = iwpool_create_empty(), finish);
  RCC(rc, finish, _init_fill(req, &n, pool));
  ret = iwn_http_response_jbn(req->http, n, 200, true);

finish:
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return ret;
}

iwrc grh_route_users(const struct iwn_wf_route *parent) {
  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .handler = _init,
    .pattern = "/init",
  }, 0);
}
