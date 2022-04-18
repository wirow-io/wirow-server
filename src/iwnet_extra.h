#pragma once
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

#include <ejdb2/jbl.h>
#include <iwnet/iwn_ws_server.h>

#define GRH_USER_DATA_TYPE_WS            0x01U
#define GRH_USER_DATA_TYPE_WB            0x02U
#define GRH_USER_DATA_TYPE_WSROOM_MEMBER 0x03U

#define GRH_USER_DATA_FIELDS              \
  int type;                               \
  void (*close)(struct grh_user_data*);   \
  void (*dispose)(struct grh_user_data*); \
  struct grh_user_data *next;             \
  void *data

struct grh_user_data {
  GRH_USER_DATA_FIELDS;
};

int iwn_http_response_gz(
  struct iwn_http_req *req,
  const char          *content_type,
  const char          *data,
  ssize_t              data_len,
  bool                 gzip);

int iwn_http_response_jbn(
  struct iwn_http_req *req,
  JBL_NODE             jbn,
  int                  code,
  bool                 gzip);

int iwn_http_response_jbl(
  struct iwn_http_req *req,
  JBL                  jbl,
  int                  code,
  bool                 gzip);

bool iwn_ws_server_write_jbl(struct iwn_ws_sess *ws, JBL json);

bool iwn_ws_server_write_jbn(struct iwn_ws_sess *ws, JBL_NODE json);

void grh_req_data_add(struct iwn_http_req *req, struct grh_user_data *h);

void grh_req_data_set(struct iwn_http_req *req, struct grh_user_data *h);

void grh_req_data_dispose(struct iwn_http_req *req, int type);

void grh_req_data_dispose_all(struct iwn_http_req *req);

struct grh_user_data* grh_req_data_find(struct iwn_http_req *req, int type);
