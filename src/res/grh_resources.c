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

#include "grh_resources.h"
#include "grh_resources_impl.h"
#include "grh_users.h"

#include "data_front_index_html.inc"
#include "data_front_bundle_js_gz.inc"

#include <iwnet/iwn_wf_files.h>

#include <stdlib.h>

static struct resource _res[] = {
  RES("/",           0x2b5d4,    "text/html;charset=UTF-8",              data_front_index_html,   false, false),
  RES("/index.html", 0xe114b9cf, "text/html;charset=UTF-8",              data_front_index_html,   false, true),
  RES("/bundle.js",  0x89fc95d9, "application/javascript;charset=UTF-8", data_front_bundle_js_gz, true,  false)
};

static int _resources(struct iwn_wf_req *req, void *user_data) {
  uint32_t hash = _hash(req->path);
  for (int i = 0; i < sizeof(_res) / sizeof(_res[0]); ++i) {
    struct resource *r = &_res[i];
    if (r->hash == hash && strcmp(req->path, r->path) == 0) {
      return _serve(r, req);
    }
  }
  return 0;
}

iwrc grh_route_resources(struct iwn_wf_route *parent) {
  iwrc rc = 0;
  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .handler = _resources,
    .tag = "resources"
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/users",
    .flags = IWN_WF_METHODS_ALL,
  }, &parent));

  RCC(rc, finish, grh_route_users(parent));

finish:
  return rc;
}

iwrc grh_route_resources_overlays(struct iwn_wf_route *parent) {
  iwrc rc = 0;
  for (const char **rr = g_env.private_overlays.roots; rr && *rr; ++rr) {
    RCC(rc, finish, iwn_wf_route(iwn_wf_route_dir_attach(&(struct iwn_wf_route) {
      .parent = parent,
      .flags = IWN_WF_MATCH_PREFIX | IWN_WF_GET | IWN_WF_HEAD,
      .tag = *rr
    }, *rr), 0));
  }

finish:
  return rc;
}
