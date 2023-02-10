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

#include "lic_env.h"
#include "gr_sentry.h"
#include "grh.h"
#include "gr_crypt.h"
#include "utils/xcurl.h"

#include <iowow/iwutils.h>
#include <iowow/iwlog.h>

#include <iwnet/iwnet.h>

#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef SENTRY_DSN
#define SENTRY_BUILD_STATIC 1
#include <sentry.h>
#endif

#ifdef SENTRY_FRONT_ENVELOPE

char* _get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen) {
  if (sa->sa_family == AF_INET) {
    inet_ntop(AF_INET, &(((struct sockaddr_in*) sa)->sin_addr), s, maxlen);
  } else if (sa->sa_family == AF_INET6) {
    inet_ntop(AF_INET6, &(((struct sockaddr_in6*) sa)->sin6_addr), s, maxlen);
  } else {
    strncpy(s, "null", maxlen);
  }
  return s;
}

iwrc _wrap_envelope_header(JBL_NODE n, IWPOOL *pool) {
  JBL_NODE n2;
  if (!jbn_at(n, "/dsn", &n2)) {
    jbn_remove_item(n, n2);
  }
  return jbn_add_item_str(n, "dsn", SENTRY_FRONT_DSN, -1, 0, pool);
}

iwrc _wrap_envelope_event_payload(JBL_NODE n, IWPOOL *pool) {
  iwrc rc = 0;
  JBL_NODE n2, n3;

  { // Set release
    if (!jbn_at(n, "release", &n2)) {
      jbn_remove_item(n, n2);
    }
#ifdef GIT_REVISION
    RCC(rc, finish, jbn_add_item_str(n, "release", XSTR(GIT_REVISION), -1, 0, pool));
#else
    RCC(rc, finish, jbn_add_item_str(n, "release", "dev", -1, 0, pool));
#endif
  }

  { // Set tags
    if (!jbn_at(n, "tags", &n2)) {
      if (n2->type != JBV_OBJECT) {
        jbn_remove_item(n, n2);
        RCC(rc, finish, jbn_add_item_obj(n, "tags", &n2, pool));
      }
    } else {
      RCC(rc, finish, jbn_add_item_obj(n, "tags", &n2, pool));
    }

    if (!jbn_at(n2, "license", &n3)) {
      jbn_remove_item(n2, n3);
    }
#ifdef LICENSE_ID
    RCC(rc, finish, jbn_add_item_str(n2, "license", LICENSE_ID, -1, 0, pool));
#endif

    if (!jbn_at(n2, "build_type", &n3)) {
      jbn_remove_item(n2, n3);
    }
    RCC(rc, finish, jbn_add_item_str(n2, "build_type", XSTR(GR_BUILD_TYPE), -1, 0, pool));
  }

  { // Set license context
    if (!jbn_at(n, "contexts", &n2)) {
      if (n2->type != JBV_OBJECT) {
        jbn_remove_item(n, n2);
        RCC(rc, finish, jbn_add_item_obj(n, "contexts", &n2, pool));
      }
    } else {
      RCC(rc, finish, jbn_add_item_obj(n, "contexts", &n2, pool));
    }

    if (!jbn_at(n2, "license", &n3)) {
      jbn_remove_item(n2, n3);
    }
#ifdef LICENSE_ID
    RCC(rc, finish, jbn_add_item_obj(n2, "license", &n3, pool));
    RCC(rc, finish, jbn_add_item_str(n3, "ID", LICENSE_ID, -1, 0, pool));
    RCC(rc, finish, jbn_add_item_str(n3, "Owner", LICENSE_OWNER, -1, 0, pool));
    RCC(rc, finish, jbn_add_item_str(n3, "Login", LICENSE_LOGIN, -1, 0, pool));
    RCC(rc, finish, jbn_add_item_str(n3, "Email", LICENSE_EMAIL, -1, 0, pool));
    RCC(rc, finish, jbn_add_item_str(n3, "Terms", LICENSE_TERMS, -1, 0, pool));
#endif
  }

finish:
  return rc;
}

iwrc _wrap_envelope_session_payload(JBL_NODE n, IWPOOL *pool) {
  iwrc rc = 0;
  JBL_NODE n2, n3;

  { // Set attrs
    if (!jbn_at(n, "attrs", &n2)) {
      if (n2->type != JBV_OBJECT) {
        jbn_remove_item(n, n2);
        RCC(rc, finish, jbn_add_item_obj(n, "attrs", &n2, pool));
      }
    } else {
      RCC(rc, finish, jbn_add_item_obj(n, "attrs", &n2, pool));
    }

    if (!jbn_at(n2, "release", &n3)) {
      jbn_remove_item(n2, n3);
    }
#ifdef GIT_REVISION
    RCC(rc, finish, jbn_add_item_str(n2, "release", XSTR(GIT_REVISION), -1, 0, pool));
#else
    RCC(rc, finish, jbn_add_item_str(n2, "release", "dev", -1, 0, pool));
#endif
  }

finish:
  return rc;
}

iwrc _wrap_envelope_data(const char *data_in, size_t data_in_size, IWXSTR **data_out) {
  iwrc rc = 0;
  IWPOOL *pool = 0;
  JBL_NODE n, n2, n3;
  char *data_in_copy, *data_in_end, *data_ptr1, *data_ptr2;

  RCA(*data_out = iwxstr_new(), finish);

  RCA(pool = iwpool_create(data_in_size * 3), finish);
  data_in_copy = iwpool_strndup(pool, data_in, data_in_size, &rc);
  RCGO(rc, finish);
  data_in_end = strchr(data_in_copy, '\0');

  // https://develop.sentry.dev/sdk/envelopes/
  // Message format is:
  //   Envelope = Headers { "\n" Item } [ "\n" ] ;
  //   Item = Headers "\n" Payload ;
  //   Payload = { * } ;

  { // Update envelope header data
    data_ptr1 = data_in_copy;
    data_in_copy = strchr(data_in_copy, '\n');
    data_in_copy || (data_in_copy = strchr(data_ptr1, '\0'));
    *data_in_copy = '\0';
    RCC(rc, finish, jbn_from_json(data_ptr1, &n, pool));
    RCC(rc, finish, _wrap_envelope_header(n, pool));
    RCC(rc, finish, jbn_as_json(n, jbl_xstr_json_printer, *data_out, 0));
  }

  RCC(rc, finish, iwxstr_cat(*data_out, "\n", 1));

  while (data_in_copy && data_in_copy < data_in_end) {
    { // Find end of item header and break if there is no payload (bad format)
      data_ptr1 = data_in_copy + 1;
      data_in_copy = strchr(data_ptr1, '\n');
      data_in_copy || (data_in_copy = strchr(data_ptr1, '\0'));
      if (*data_in_copy == '\0') {
        break;
      }
      *data_in_copy = '\0';
    }

    RCC(rc, finish, jbn_from_json(data_ptr1, &n, pool));

    { // Find end of item payload
      data_ptr2 = data_in_copy + 1;
      if (!jbn_at(n, "/length", &n2) && n2->type == JBV_I64) {
        data_in_copy += n2->vi64;
        data_in_copy = MIN(data_in_copy, data_in_end);
      } else {
        data_in_copy = strchr(data_ptr2, '\n');
        data_in_copy || (data_in_copy = strchr(data_ptr2, '\0'));
      }
      *data_in_copy = '\0';
    }

    // Item type "event" should be modified (and maybe some else but I dont know)
    if (  !jbn_at(n, "/type", &n3) && n3->type == JBV_STR
       && (!strcmp(n3->vptr, "event") || !strcmp(n3->vptr, "transaction"))) {
      // Remove length parameter since payload is modified
      // Length parameter is optional if payload has no '\n'
      n2 && (jbn_remove_item(n, n2), 0);

      RCC(rc, finish, jbn_from_json(data_ptr2, &n3, pool));
      RCC(rc, finish, _wrap_envelope_event_payload(n3, pool));

      RCC(rc, finish, jbn_as_json(n, jbl_xstr_json_printer, *data_out, 0));
      RCC(rc, finish, iwxstr_cat(*data_out, "\n", 1));
      RCC(rc, finish, jbn_as_json(n3, jbl_xstr_json_printer, *data_out, 0));
    } else if (  n3 && n3->type == JBV_STR
              && (!strcmp(n3->vptr, "session") || !strcmp(n3->vptr, "sessions"))) {
      n2 && (jbn_remove_item(n, n2), 0);

      RCC(rc, finish, jbn_from_json(data_ptr2, &n3, pool));
      RCC(rc, finish, _wrap_envelope_session_payload(n3, pool));

      RCC(rc, finish, jbn_as_json(n, jbl_xstr_json_printer, *data_out, 0));
      RCC(rc, finish, iwxstr_cat(*data_out, "\n", 1));
      RCC(rc, finish, jbn_as_json(n3, jbl_xstr_json_printer, *data_out, 0));
    } else {
      RCC(rc, finish, iwxstr_cat2(*data_out, data_ptr1));
      RCC(rc, finish, iwxstr_cat(*data_out, "\n", 1));
      RCC(rc, finish, iwxstr_cat2(*data_out, data_ptr2));
    }
    RCC(rc, finish, iwxstr_cat(*data_out, "\n", 1));
  }

finish:
  iwpool_destroy(pool);
  return rc;
}

static int _handler_envelope(struct iwn_wf_req *req, void *d) {
  if (!g_env.log.report_errors) {
    return iwn_http_response_write(req->http, 200, "application/json", "{}", IW_LLEN("{}"));
  }
  iwrc rc = 0;
  int ret = 500;
  CURLcode cc = 0;
  struct curl_slist *headers = 0;
  CURL *curl = 0;
  IWXSTR *out = 0, *tmp = 0, *data_out = 0;
  long code;
  char ip[256];

  RCC(rc, finish, _wrap_envelope_data(req->body, req->body_len, &data_out));
  RCA(out = iwxstr_new(), finish);
  RCA(tmp = iwxstr_new(), finish);

  if (!(curl = curl_easy_init())) {
    rc = GR_ERROR_CURL_API_FAIL;
    goto finish;
  }

  curl_easy_reset(curl);

  struct curl_blob cainfo_blob = {
    .data  = (void*) iwn_cacerts,
    .len   = iwn_cacerts_len,
    .flags = CURL_BLOB_NOCOPY,
  };

  RCC(rc, finish, iwxstr_printf(tmp, "X-Forwarded-For: %s", iwn_http_request_remote_ip(req->http)));
  RCB(finish, headers = curl_slist_append(headers, iwxstr_ptr(tmp)));

  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_URL, SENTRY_FRONT_ENVELOPE));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &cainfo_blob));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_POST, 1L));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, xcurl_body_write_xstr));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEDATA, out));

  struct xcurl_cursor dcur = {
    .rp  = iwxstr_ptr(data_out),
    .end = iwxstr_ptr(data_out) + iwxstr_size(data_out),
  };

  xcurlreq_hdr_add(data, "Expect", IW_LLEN("Expect"), "", 0);
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READFUNCTION, xcurl_read_cursor));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READDATA, &dcur));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_INFILESIZE, dcur.end - dcur.rp));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, dcur.end - dcur.rp));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, LICENSE_SSL_VERIFY));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, LICENSE_SSL_VERIFY));

  XCC(cc, finish, curl_easy_perform(curl));
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

  // TODO: ctype?
  ret = iwn_http_response_write(req->http, code, "application/json", iwxstr_ptr(out), iwxstr_size(out)) ? 1 : -1;

finish:
  iwxstr_destroy(out);
  iwxstr_destroy(tmp);
  iwxstr_destroy(data_out);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (rc) {
    iwlog_ecode_warn(rc, "Error while tunneling Sentry from js");
  }
  return ret;
}

iwrc grh_route_sentry(struct iwn_wf_route *parent) {
  struct iwn_wf_route *route_sentry;
  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/sentry",
    .flags = IWN_WF_POST,
  }, &route_sentry));

  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = route_sentry,
    .pattern = "/envelope",
    .handler = _handler_envelope,
    .flags = IWN_WF_POST,
  }, 0));

  return 0;
}

#else // SENTRY_FRONT_ENVELOPE

iwrc grh_route_sentry(struct iwn_wf_route *parent) {
  return 0;
}

#endif // SENTRY_FRONT_ENVELOPE

#ifdef SENTRY_DSN

iwrc gr_sentry_init(int argc, char **argv) {
  if (!g_env.log.report_errors) {
    return 0;
  }

  iwrc rc = 0;
  sentry_options_t *options;
  IWXSTR *tmp = 0;

  RCA(options = sentry_options_new(), finish);

  RCA(tmp = iwxstr_new(), finish);
  RCC(rc, finish, iwxstr_printf(tmp, "%s/.sentry-native", g_env.data_dir));
  sentry_options_set_database_path(options, iwxstr_ptr(tmp));

  sentry_options_set_dsn(options, SENTRY_DSN);
  RCA(sentry_options_get_dsn(options), finish);

  sentry_options_set_auto_session_tracking(options, 0);

#ifdef DEBUG
  sentry_options_set_debug(options, 1);
#endif

  sentry_options_set_ca_certs_blob(options, (void*)iwn_cacerts, iwn_cacerts_len);

#ifdef GIT_REVISION
  sentry_options_set_release(options, XSTR(GIT_REVISION));
#else
  sentry_options_set_release(options, "dev");
#endif
  RCA(sentry_options_get_release(options), finish);

  sentry_options_add_attachment(options, g_env.config_file);

  if (sentry_init(options)) {
    options = 0;
    RCC(rc, finish, IW_ERROR_FAIL);
  }
  options = 0;

  sentry_set_tag("build_type", XSTR(GR_BUILD_TYPE));

  sentry_value_t args = sentry_value_new_list();
  if (sentry_value_is_null(args) == 0) {
    for (int i = 0; i < argc; i++) {
      sentry_value_append(args, sentry_value_new_string(argv[i]));
    }
  }
  sentry_set_extra("Command", args);

#ifdef LICENSE_ID
  sentry_set_tag("license", LICENSE_ID);
  sentry_value_t lic = sentry_value_new_object();
  if (sentry_value_is_null(lic) == 0) {
    sentry_value_set_by_key(lic, "ID", sentry_value_new_string(LICENSE_ID));
    sentry_value_set_by_key(lic, "Owner", sentry_value_new_string(LICENSE_OWNER));
    sentry_value_set_by_key(lic, "Login", sentry_value_new_string(LICENSE_LOGIN));
    sentry_value_set_by_key(lic, "Email", sentry_value_new_string(LICENSE_EMAIL));
    sentry_value_set_by_key(lic, "Terms", sentry_value_new_string(LICENSE_TERMS));
  }
  sentry_set_context("license", lic);
#endif

  sentry_value_t user = sentry_value_new_object();
  if (sentry_value_is_null(user) == 0) {
#ifdef LICENSE_ID
    sentry_value_set_by_key(user, "id", sentry_value_new_string(LICENSE_ID));
    sentry_value_set_by_key(user, "email", sentry_value_new_string(LICENSE_EMAIL));
#else
    sentry_value_set_by_key(user, "id", sentry_value_new_string("dev"));
#endif
  }
  sentry_set_user(user);

finish:
  sentry_options_free(options);
  iwxstr_destroy(tmp);
  return rc;
}

iwrc gr_sentry_dispose(void) {
  sentry_close();
  return 0;
}

#else // SENTRY_DSN

iwrc gr_sentry_init(int argc, char **argv) {
  return 0;
}

iwrc gr_sentry_dispose(void) {
  return 0;
}

#endif // SENTRY_DSN
