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

#include "grh.h"
#include <iowow/iwhmap.h>
#include <iowow/iwuuid.h>

#define SIMPLE_HANDLER_FINISH_RC(n__)                               \
  if (rc) {                                                         \
    if (error) {                                                    \
      iwlog_ecode_debug(rc, "Handler error: %s", error);            \
    } else {                                                        \
      iwlog_ecode_error2(rc, "Handler error: error.unspecified");   \
      error = "error.unspecified";                                  \
    }                                                               \
  }                                                                 \
  rc = grh_ws_send_confirm2(ctx, n__, error)

#define SIMPLE_HANDLER_FINISH_RET(n__)                              \
  SIMPLE_HANDLER_FINISH_RC(n__);                                    \
  return rc

struct ws_session;
struct ws_event_listener_slot;

#define WS_EVENT_CLOSE 0x01U

typedef void (*ws_event_listener_fn)(int event, struct ws_session *wss, void *data);

struct ws_session {
  GRH_USER_DATA_FIELDS;
  struct iwn_ws_sess *ws;
  struct ws_event_listener_slot *listeners;
  int  wsid;    ///< WS id
  char uuid[IW_UUID_STR_LEN + 1];
  bool initialized;
};

struct ws_message_ctx {
  struct ws_session *wss;
  const char *cmd;
  const char *hook;
  JBL_NODE    payload;
  IWPOOL     *pool;
};

typedef bool (*ws_clients_visitor)(struct ws_session *wss, void *data);

typedef iwrc (*wsh_handler_fn)(struct ws_message_ctx *ctx, void *data);

iwrc grh_wss_add_event_listener(struct ws_session *wss, ws_event_listener_fn listener, void *data);

struct grh_user_data* grh_wss_get_data_of_type(struct ws_session *wss, int data_type);

void grh_wss_set_data_of_type(struct ws_session *wss, struct grh_user_data *data);

void grh_wss_unset_data_of_type(struct ws_session *wss, int data_type);

iwrc grh_ws_register_wsh_handler(
  const char *cmd, const char *name, wsh_handler_fn wsh, void *data,
  void (*dispose)(void*));

/**
 * @brief WS client visitor. Visiting is performed while visitor returns `true`
 * @warning Visiting is performend in READ locked context.
 *          So you cannot use send_* functions since they takes a read lock too.
 */
iwrc grh_ws_visit_clients(ws_clients_visitor visitor, void *data);

iwrc grh_ws_peek_client_by_wsid(int wsid, ws_clients_visitor visitor, void *data);

iwrc grh_ws_peek_client_by_session_uuid(const char *uuid, ws_clients_visitor visitor, void *data);

/**
 * @brief Send a message to all active websocket sessions.
 *
 * @param data Message data pointer.
 * @param len  Message length or `-1` if length of message will be computed by `strlen()`
 * @return iwrc Not zero in error.
 */
iwrc grh_ws_send_all(const char *data, ssize_t len);

iwrc grh_ws_send_all_room_participants(int64_t room_id, const char *data, ssize_t len);

/**
 * @brief Send a message to user indentified by user document id.
 *
 * @param id Websocket session id
 * @param data Message data pinter.
 * @param len  Message length or `-1` if length of message will be computed by `strlen()`
 * @return iwrc Not zero in error.
 */
void grh_ws_send_by_wsid(int wsid, const char *data, ssize_t len);

/**
 * @brief Send a message to WS session indentified by uuid
 *
 * @param uuid WS session uuid
 * @param data Message data pinter.
 * @param len  Message length or `-1` if length of message will be computed by `strlen()`
 * @return iwrc Not zero in error.
 */
void grh_ws_send_by_uuid(const char *uuid, const char *data, ssize_t len);

iwrc grh_ws_send(struct ws_message_ctx *ctx, JBL_NODE json);

iwrc grh_ws_send_confirm(struct ws_message_ctx *ctx, const char *error);

iwrc grh_ws_send_confirm2(struct ws_message_ctx *ctx, JBL_NODE n, const char *error);

iwrc grh_route_ws(struct iwn_wf_route *parent);

void grh_ws_close(void);
