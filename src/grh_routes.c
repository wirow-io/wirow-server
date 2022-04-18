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

#include "grh_routes.h"
#include "gr_sentry.h"
#include "grh_auth.h"
#include "grh_monsters.h"
#include "grh_room.h"
#include "grh_uploads.h"
#include "grh_ws.h"
#include "lic_env.h"
#include "res/grh_resources.h"
#include "res/grh_resources_adm.h"
#include "res/grh_resources_pub.h"
#include "acme/acme.h"

#if (ENABLE_RECORDING == 1)
#include "rec/grh_recording.h"
#endif

#if (ENABLE_WHITEBOARD == 1)
#include "wb/wb.h"
#endif

static void _wf80_dispose(struct iwn_wf_ctx *ctx, void *user_data) {
  g_env.wf80 = 0;
}

static void _wf_dispose(struct iwn_wf_ctx *ctx, void *data) {
  g_env.wf = 0;
}

static iwrc _configure_wf80(void) {
  struct iwn_wf_route *basic;
  RCR(iwn_wf_create(&(struct iwn_wf_route) {
    .tag = "wf80_root",
    .handler_dispose = _wf80_dispose
  }, &g_env.wf80));
  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .tag = "wf80_basic",
    .ctx = g_env.wf80,
    .flags = IWN_WF_METHODS_ALL,
  }, &basic));
  return grh_route_acme_challenge(basic);
}

/// Will be called if no routes where handled / found
static int _handler_wf_root(struct iwn_wf_req *req, void *user_data) {
  int code = iwn_http_response_code_get(req->http);
  if (code == 401 || code == 403) {
    iwn_http_response_header_set(req->http, "location", "/login.html", IW_LLEN("/login.html"));
    return 302;
  }
  return 404;
}

/// Performs basic request setup
static int _handler_basic(struct iwn_wf_req *req, void *data) {
  req->http->on_request_dispose = grh_req_data_dispose_all;
  req->http->session_cookie_max_age_sec = g_env.session_cookies_max_age;
  req->http->session_cookie_params = "; samesite=none";
  grh_auth_request_init(req);
  for (struct gr_pair *h = g_env.http_headers; h; h = h->next) {
    if (h->name && h->sv) {
      iwn_http_response_header_add(req->http, h->name, h->sv, -1);
    }
  }
  return 0;
}

iwrc grh_routes_configure(void) {
  iwrc rc = 0;
  if (  g_env.https_redirect_port > 0
     && (g_env.domain_name || (g_env.cert_file && g_env.cert_key_file))) {
    RCR(_configure_wf80());
  }
  struct iwn_wf_route *basic, *auth;

  RCC(rc, finish, iwn_wf_create(&(struct iwn_wf_route) {
    .handler = _handler_wf_root,
    .handler_dispose = _wf_dispose,
    .tag = "wf_root"
  }, &g_env.wf));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .ctx = g_env.wf,
    .handler = _handler_basic,
    .flags = IWN_WF_METHODS_ALL,
    .tag = "wf_basic",
  }, &basic));

  RCC(rc, finish, grh_route_sentry(basic));
  RCC(rc, finish, grh_route_resources_pub(basic));
#if (ENABLE_WHITEBOARD == 1)
  RCC(rc, finish, grh_route_wb(basic));
#endif

  RCC(rc, finish, grh_auth_routes(basic, &auth));
  RCC(rc, finish, grh_route_resources_overlays(auth));
  RCC(rc, finish, grh_route_resources(auth));
  RCC(rc, finish, grh_route_monsters(auth));
  RCC(rc, finish, grh_route_resources_adm(auth));
  RCC(rc, finish, grh_route_room(auth));
  RCC(rc, finish, grh_route_uploads(auth));
  RCC(rc, finish, grh_route_ws(auth));

#if (ENABLE_RECORDING == 1)
  RCC(rc, finish, grh_route_recording(auth));
#endif

  if (g_env.log.verbose) {
    if (g_env.wf80) {
      fprintf(stdout, "\n");
      iwn_wf_route_print(g_env.wf80->root, stdout);
    }
    fprintf(stdout, "\n");
    iwn_wf_route_print(g_env.wf->root, stdout);
    fprintf(stdout, "\n");
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc;
}
