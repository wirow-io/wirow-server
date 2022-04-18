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

#include "grh_resources_adm.h"
#include "grh_resources_impl.h"
#include "adm/grh_adm.h"

static const unsigned char admin_html[]
  = "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<title>Wirow Admin Console</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1' />"
    "<link href='fonts.css' rel='stylesheet' type='text/css' />"
    "<link href='admin.css' rel='stylesheet' type='text/css' />"
    "</head>"
    "<body>"
    "<script src='admin.js'></script>"
    "</body>"
    "</html>";

const unsigned int admin_html_len = sizeof(admin_html) - 1;

#include "data_front_admin_js_gz.inc"
#include "data_front_admin_css_gz.inc"

static struct resource _res[] = {
  RES("/admin",      0x7c9576bd, "text/html;charset=UTF-8",              admin_html,              false, true),
  RES("/admin.html", 0x9ec99f60, "text/html;charset=UTF-8",              admin_html,              false, true),
  RES("/admin.css",  0x23d77b14, "text/css;charset=UTF-8",               data_front_admin_css_gz, true,  false),
  RES("/admin.js",   0xf1922b68, "application/javascript;charset=UTF-8", data_front_admin_js_gz,  true,  false),
};

static bool _check_access(struct resource *r, struct iwn_wf_req *req) {
  if (!(req->http->user_flags & REQ_FLAG_ROLE_ADMIN)) {
    if (r && ((strcmp(r->path, "/admin") == 0) || (strcmp(r->path, "/admin.html") == 0))) {
      iwn_http_response_code_set(req->http, 403);
    }
    return false;
  }
  return true;
}

static int _serve_check_access(struct resource *r, struct iwn_wf_req *req) {
  if (_check_access(r, req)) {
    return _serve(r, req);
  } else {
    return 0;
  }
}

static int _handler_resources_admin(struct iwn_wf_req *req, void *data) {
  if (!(req->flags & IWN_WF_GET)) {
    return 0;
  }
  uint32_t hash = _hash(req->path);
  for (int i = 0; i < sizeof(_res) / sizeof(_res[0]); ++i) {
    struct resource *r = &_res[i];
    if ((r->hash == hash) && (strcmp(req->path, r->path) == 0)) {
      return _serve_check_access(r, req);
    }
  }
  return 0;
}

static int _handler_admin(struct iwn_wf_req *req, void *data) {
  if (!(req->http->user_flags & REQ_FLAG_ROLE_ADMIN)) {
    return -2; // Disable processing of child routes
  } else {
    return 0;
  }
}

iwrc grh_route_resources_adm(struct iwn_wf_route *parent) {
  iwrc rc;

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .handler = _handler_resources_admin,
    .tag = "resources_admin"
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/admin",
    .handler = _handler_admin,
    .flags = IWN_WF_METHODS_ALL,
  }, &parent));

  RCC(rc, finish, grh_route_adm(parent));

finish:
  return rc;
}
