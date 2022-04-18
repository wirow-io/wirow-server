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

#include <iwnet/iwn_curl.h>
#include <ejdb2/jbl.h>

struct xcurlresp {
  IWXSTR     *body;
  JBL_NODE    json;
  const char *ctype;
  IWPOOL     *pool;
  long   response_code;
  IWLIST headers;
};

static iwrc xcurlresp_init(struct xcurlresp *resp) {
  resp->body = iwxstr_new();
  if (!resp->body) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  iwlist_init(&resp->headers, 8);
  return 0;
}

static void xcurlresp_destroy_keep(struct xcurlresp *resp) {
  if (resp) {
    iwxstr_destroy(resp->body);
    iwlist_destroy_keep(&resp->headers);
    iwpool_destroy(resp->pool);
    resp->body = 0;
    resp->json = 0;
    resp->pool = 0;
    resp->ctype = 0;
  }
}
