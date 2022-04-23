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

#include "gr.h"
#include <stdlib.h>
#include <string.h>
#include <ejdb2/jbl.h>
#include <pthread.h>

typedef int32_t wrc_resource_t;
typedef uint32_t wrc_event_handler_t;

struct wrc_msg;

typedef void (*wrc_msg_processed_handler)(struct wrc_msg *msg);

typedef enum {
  WRC_EVT_PAYLOAD,  // Worker payload event

  WRC_EVT_STARTED,
  WRC_EVT_SHUTDOWN,
  WRC_EVT_TRACE,
  WRC_EVT_WORKER_SHUTDOWN,
  WRC_EVT_WORKER_LAUNCHED,
  WRC_EVT_ROUTER_CREATED,
  WRC_EVT_ROUTER_CLOSED,
  WRC_EVT_OBSERVER_CREATED,
  WRC_EVT_OBSERVER_PAUSED,
  WRC_EVT_OBSERVER_RESUMED,
  WRC_EVT_OBSERVER_CLOSED,
  WRC_EVT_TRANSPORT_CREATED,
  WRC_EVT_TRANSPORT_UPDATED,
  WRC_EVT_TRANSPORT_CLOSED,
  WRC_EVT_TRANSPORT_ICE_STATE_CHANGE,
  WRC_EVT_TRANSPORT_ICE_SELECTED_TUPLE_CHANGE,
  WRC_EVT_TRANSPORT_DTLS_STATE_CHANGE,
  WRC_EVT_TRANSPORT_SCTP_STATE_CHANGE,
  WRC_EVT_TRANSPORT_TUPLE,
  WRC_EVT_TRANSPORT_RTCPTUPLE,
  WRC_EVT_PRODUCER_CREATED,
  WRC_EVT_PRODUCER_VIDEO_ORIENTATION_CHANGE,
  WRC_EVT_PRODUCER_CLOSED,
  WRC_EVT_PRODUCER_RESUME,
  WRC_EVT_PRODUCER_PAUSE,
  WRC_EVT_CONSUMER_PRODUCER_RESUME,
  WRC_EVT_CONSUMER_PRODUCER_PAUSE,
  WRC_EVT_CONSUMER_PAUSE,
  WRC_EVT_CONSUMER_RESUME,
  WRC_EVT_RESOURCE_SCORE,     // NOTE: Consumer or Producer score
  WRC_EVT_CONSUMER_LAYERSCHANGE,
  WRC_EVT_CONSUMER_CLOSED,
  WRC_EVT_CONSUMER_CREATED,
  WRC_EVT_AUDIO_OBSERVER_SILENCE,
  WRC_EVT_AUDIO_OBSERVER_VOLUMES,
  WRC_EVT_ACTIVE_SPEAKER,
  WRC_EVT_ROOM_CREATED,
  WRC_EVT_ROOM_CLOSED,
  WRC_EVT_ROOM_MEMBER_JOIN,
  WRC_EVT_ROOM_MEMBER_LEFT,
  WRC_EVT_ROOM_MEMBER_MUTE,
  WRC_EVT_ROOM_MEMBER_MSG,
  WRC_EVT_ROOM_RECORDING_ON,
  WRC_EVT_ROOM_RECORDING_OFF,
  WRC_EVT_ROOM_RECORDING_PP,
} wrc_event_e;

/**
 * @brief WRC Event handler.
 * @param evt Event type
 * @param resource_id Optional event related resource identifier
 * @param event_extra Optional event related extra data
 * @param op Opaque data passed in handler registration
 */
typedef iwrc (*wrc_event_handler)(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op);

typedef enum {
  WRC_MSG_GENERIC,
  WRC_MSG_WORKER,
  WRC_MSG_EVENT,
  WRC_MSG_PAYLOAD,
} wrc_msg_type_e;

typedef enum {
  WRC_CMD_NONE,
  WRC_CMD_WORKER_DUMP,
  WRC_CMD_WORKER_GET_RESOURCE_USAGE,
  WRC_CMD_WORKER_UPDATE_SETTINGS,
  WRC_CMD_WORKER_ROUTER_CREATE,
  WRC_CMD_ROUTER_CLOSE,
  WRC_CMD_ROUTER_DUMP,
  WRC_CMD_ROUTER_CREATE_WEBRTC_TRANSPORT,
  WRC_CMD_ROUTER_CREATE_PLAIN_TRANSPORT,
  WRC_CMD_ROUTER_CREATE_PIPE_TRANSPORT,
  WRC_CMD_ROUTER_CREATE_DIRECT_TRANSPORT,
  WRC_CMD_ROUTER_CREATE_AUDIO_LEVEL_OBSERVER,
  WRC_CMD_ROUTER_CREATE_ACTIVE_SPEAKER_OBSERVER,
  WRC_CMD_TRANSPORT_CLOSE,
  WRC_CMD_TRANSPORT_DUMP,
  WRC_CMD_TRANSPORT_GET_STATS,
  WRC_CMD_TRANSPORT_CONNECT,
  WRC_CMD_TRANSPORT_SET_MAX_INCOMING_BITRATE,
  WRC_CMD_TRANSPORT_RESTART_ICE,
  WRC_CMD_TRANSPORT_PRODUCE,
  WRC_CMD_TRANSPORT_CONSUME,
  WRC_CMD_TRANSPORT_PRODUCE_DATA,
  WRC_CMD_TRANSPORT_CONSUME_DATA,
  WRC_CMD_TRANSPORT_ENABLE_TRACE_EVENT,
  WRC_CMD_PRODUCER_CLOSE,
  WRC_CMD_PRODUCER_DUMP,
  WRC_CMD_PRODUCER_GET_STATS,
  WRC_CMD_PRODUCER_PAUSE,
  WRC_CMD_PRODUCER_RESUME,
  WRC_CMD_PRODUCER_ENABLE_TRACE_EVENT,
  WRC_CMD_CONSUMER_CLOSE,
  WRC_CMD_CONSUMER_DUMP,
  WRC_CMD_CONSUMER_GET_STATS,
  WRC_CMD_CONSUMER_PAUSE,
  WRC_CMD_CONSUMER_RESUME,
  WRC_CMD_CONSUMER_SET_PREFERRED_LAYERS,
  WRC_CMD_CONSUMER_SET_PRIORITY,
  WRC_CMD_CONSUMER_REQUEST_KEY_FRAME,
  WRC_CMD_CONSUMER_ENABLE_TRACE_EVENT,
  WRC_CMD_DATA_PRODUCER_CLOSE,
  WRC_CMD_DATA_PRODUCER_DUMP,
  WRC_CMD_DATA_PRODUCER_GET_STATS,
  WRC_CMD_DATA_CONSUMER_CLOSE,
  WRC_CMD_DATA_CONSUMER_DUMP,
  WRC_CMD_DATA_CONSUMER_GET_STATS,
  WRC_CMD_DATA_CONSUMER_GET_BUFFERED_AMOUNT,
  WRC_CMD_DATA_CONSUMER_SET_BUFFERED_AMOUNT_LOW_THRESHOLD,
  WRC_CMD_RTP_OBSERVER_CLOSE,
  WRC_CMD_RTP_OBSERVER_PAUSE,
  WRC_CMD_RTP_OBSERVER_RESUME,
  WRC_CMD_RTP_OBSERVER_ADD_PRODUCER,
  WRC_CMD_RTP_OBSERVER_REMOVE_PRODUCER,
} wrc_worker_cmd_e;

typedef struct wrc_worker_input {
  wrc_worker_cmd_e cmd;
  JBL internal;
  JBL data;
} wrc_worker_input_t;

typedef struct wrc_event_input {
  wrc_event_e    evt;
  wrc_resource_t resource_id;
  JBL data;
} wrc_event_input_t;

typedef enum {
  WRC_PAYLOAD_PRODUCER_SEND,
  WRC_PAYLOAD_RTP_PACKET_SEND,
  WRC_PAYLOAD_RTCP_PACKET_SEND,
} wrc_payload_type_e;

typedef struct wrc_payload_input {
  wrc_payload_type_e type;
  JBL   internal;
  JBL   data;
  char *payload;
  const char *const_payload;
  size_t      payload_len;
} wrc_payload_input_t;

typedef struct wrc_worker_output {
  JBL data;
} wrc_worker_output_t;

typedef struct wrc_event_output {
  wrc_event_e    evt;
  wrc_resource_t resource_id;
  JBL data;
} wrc_event_output_t;

typedef struct wrc_msg {
  iwrc rc;
  wrc_msg_type_e type;
  wrc_resource_t resource_id;
  wrc_msg_processed_handler handler;

  union {
    wrc_event_input_t   event;
    wrc_payload_input_t payload;
    wrc_worker_input_t  worker;
  } input;

  union {
    wrc_worker_output_t worker;
    wrc_event_output_t  event;
  } output;
} wrc_msg_t;

const char* wrc_event_name(wrc_event_e evt);

void wrc_msg_destroy(wrc_msg_t *msg);

wrc_msg_t* wrc_msg_create(const wrc_msg_t *proto);

iwrc wrc_send(wrc_msg_t *msg);

iwrc wrc_send_and_wait(wrc_msg_t *msg, int timeout_sec);

iwrc wrc_notify_event_handlers(wrc_event_e evt, wrc_resource_t resource_id, JBL data);

iwrc wrc_add_event_handler(wrc_event_handler event_handler, void *op, wrc_event_handler_t *oid);

iwrc wrc_remove_event_handler(wrc_event_handler_t id);

void wrc_worker_kill(wrc_resource_t id);

iwrc wrc_worker_acquire(wrc_resource_t *out_id);

void wrc_ajust_load_score(wrc_resource_t id, int load_score);

void wrc_register_uuid_resolver(wrc_resource_t (*resolver)(const char *uuid));

iwrc wrc_init(void);

void wrc_shutdown(void);

void wrc_close(void);
