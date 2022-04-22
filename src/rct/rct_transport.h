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

#include "rct_router.h"

typedef struct rct_transport_webrtc_connect {
  const char *role;
  struct rct_dtls_fp {
    rct_hash_func_e algorithm;
    const char     *value;
    struct rct_dtls_fp *next;
  } *fingerprints;
} rtc_transport_webrtc_connect_t;

typedef struct rct_transport_plain_connect {
  const char *ip;
  const char *key_base64;
  int port;
  int rtcp_port;
  rtc_srtp_crypto_suite_e crypto_suite;
} rct_transport_plain_connect_t;

typedef struct rct_transport_connect {
  IWPOOL *pool;
  int     type;
  union {
    rtc_transport_webrtc_connect_t wbrtc;
    rct_transport_plain_connect_t  plain;
  };
} rct_transport_connect_t;

iwrc rct_transport_dump(wrc_resource_t transport_id, JBL *dump_out);

iwrc rct_transport_stats(wrc_resource_t transport_id, JBL *result_out);

iwrc rct_transport_set_max_incoming_bitrate(wrc_resource_t transport_id, uint32_t bitrate);

#define RCT_TRANSPORT_TRACE_EVT_PROBATION 0x01
#define RCT_TRANSPORT_TRACE_EVT_BWE       0x02

iwrc rct_transport_enable_trace_events(wrc_resource_t transport_id, uint32_t types);

iwrc rct_transport_connect_spec_from_json(JBL_NODE json, rct_transport_connect_t **spec_out);

iwrc rct_transport_connect(wrc_resource_t transport_id, rct_transport_connect_t *spec);

iwrc rct_transport_close(wrc_resource_t transport_id);

iwrc rct_transport_close_async(wrc_resource_t transport_id);

//
// Common
//

iwrc rct_transport_complete_registration(JBL data, rct_transport_t *transport);

int rct_transport_next_id_lk(rct_transport_t *transport);

int rct_transport_next_mid_lk(rct_transport_t *transport);

//
// WebRTC transport
//

iwrc rct_transport_webrtc_spec_create(uint32_t flags, rct_transport_webrtc_spec_t **spec_out);

/**
 * @brief Creates new webrtc transport and adds first pair of optional listen/announced IP.
 *
 * @param listen_ip Optional listen IP.
 * @param announced_ip Optional announced IP.
 * @param spec_out WebRTC transport spec placeholder.
 */
iwrc rct_transport_webrtc_spec_create2(
  uint32_t flags, const char *listen_ip, const char *announced_ip,
  rct_transport_webrtc_spec_t **spec_out);

iwrc rct_transport_webrtc_spec_add_ip(rct_transport_webrtc_spec_t *spec, const char *ip, const char *announced_ip);

iwrc rct_transport_webrtc_create(
  rct_transport_webrtc_spec_t *spec, wrc_resource_t router_id,
  wrc_resource_t *transport_id_out);

iwrc rct_transport_webrtc_restart_ice(wrc_resource_t transport_id, JBL *result_out);

//
// Direct transport
//

iwrc rct_transport_direct_create(wrc_resource_t router_id, uint32_t max_message_size, wrc_resource_t *transport_id_out);

iwrc rct_transport_direct_send_rtcp(wrc_resource_t transport_id, char *payload, size_t payload_len);

//
// Plain transport
//

iwrc rct_transport_plain_spec_create(
  const char                  *listen_ip,
  const char                  *announced_ip,
  rct_transport_plain_spec_t **spec_out);

iwrc rct_transport_plain_create(
  rct_transport_plain_spec_t *spec,
  wrc_resource_t              router_id,
  wrc_resource_t             *transport_id_out);

iwrc rct_transport_plain_connect_spec_create(
  const char               *ip,
  const char               *key_base64,
  rct_transport_connect_t **spec_out);

iwrc _rct_transport_plain_connect(rct_transport_plain_t *transport, rct_transport_plain_connect_t *spec);


//
// Module
//

iwrc rct_transport_module_init(void);

void rct_transport_module_close(void);
