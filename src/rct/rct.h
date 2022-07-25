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

#include "lic_env.h"
#include "wrc/wrc.h"
#include "utils/utf8.h"

#include <iowow/iwlog.h>
#include <iowow/iwuuid.h>
#include <iowow/iwpool.h>
#include <iowow/iwhmap.h>
#include <iowow/iwarr.h>
#include <iowow/iwjson.h>

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

typedef enum {
  _RCT_ERROR_START = (IW_ERROR_START + 100000UL + 1000),
  RCT_ERROR_TOO_MANY_DYNAMIC_PAYLOADS, /**< Too many dynamic payload types (RCT_ERROR_TOO_MANY_DYNAMIC_PAYLOADS) */
  RCT_ERROR_INVALID_RTP_PARAMETERS,    /**< Invalid RTP parameters (RCT_ERROR_INVALID_RTP_PARAMETERS) */
  RCT_ERROR_INVALID_PROFILE_LEVEL_ID,  /**< Invalid H264 profile level ID (RCT_ERROR_INVALID_PROFILE_LEVEL_ID) */
  RCT_ERROR_PROFILE_LEVEL_ID_MISMATCH, /**< H264 profile level ID mismatch (RCT_ERROR_PROFILE_LEVEL_ID_MISMATCH) */
  RCT_ERROR_UNSUPPORTED_CODEC,         /**< Unsupported codec (RCT_ERROR_UNSUPPORTED_CODEC) */
  RCT_ERROR_NO_RTX_ASSOCIATED_CODEC,
  /**< Cannot find assiciated media codec for given RTX payload type
     (RCT_ERROR_NO_RTX_ASSOCIATED_CODEC) */
  RCT_ERROR_INVALID_SCTP_STREAM_PARAMETERS,
  /**< Invalid SCTP Stream parameters
     (RCT_ERROR_INVALID_SCTP_STREAM_PARAMETERS) **/
  RCT_ERROR_UNBALANCED_REFS,           /**< Unbalanced resource refs (RCT_ERROR_UNBALANCED_REFS) */
  RCT_ERROR_INVALID_RESOURCE_CONFIGURATION,
  /**< Invalid resource configuration
     (RCT_ERROR_INVALID_RESOURCE_CONFIGURATION) */
  RCT_ERROR_REQUIRED_DIRECT_TRANSPORT,
  /**Transport must be of RCT_TYPE_TRANSPORT_DIRECT type to perform operation
     (RCT_ERROR_REQUIRED_DIRECT_TRANSPORT) */
  _RCT_ERROR_END,
} rct_ecode_t;


struct rct_state {
  uint32_t event_handler_id;
  uint32_t resource_seq;
  pthread_mutex_t mtx;
  JBL_NODE available_capabilities;
  IWPOOL  *pool;
  IWHMAP  *map_uuid2ptr;
  IWHMAP  *map_id2ptr;
};

extern struct gr_env g_env;
extern struct rct_state state;

#define RCT_INIT_REFS 0xffffff

#define RCT_TYPE_ROUTER           0x01U
#define RCT_TYPE_TRANSPORT_WEBRTC 0x02U
#define RCT_TYPE_TRANSPORT_PLAIN  0x04U
#define RCT_TYPE_TRANSPORT_DIRECT 0x08U
#define RCT_TYPE_TRANSPORT_PIPE   0x10U
#define RCT_TYPE_TRANSPORT_ALL    (RCT_TYPE_TRANSPORT_WEBRTC | RCT_TYPE_TRANSPORT_DIRECT | RCT_TYPE_TRANSPORT_PLAIN \
                                   | RCT_TYPE_TRANSPORT_PIPE)
#define RCT_TYPE_PRODUCER         0x20U
#define RCT_TYPE_PRODUCER_DATA    0x40U
#define RCT_TYPE_PRODUCER_ALL     (RCT_TYPE_PRODUCER | RCT_TYPE_PRODUCER_DATA)
#define RCT_TYPE_CONSUMER         0x80U
#define RCT_TYPE_CONSUMER_DATA    0x100U
#define RCT_TYPE_CONSUMER_ALL     (RCT_TYPE_CONSUMER | RCT_TYPE_CONSUMER_DATA)
#define RCT_TYPE_OBSERVER_AL      0x200U // Audio level
#define RCT_TYPE_OBSERVER_AS      0x400U // Active speaker
#define RCT_TYPE_OBSERVER_ALL     (RCT_TYPE_OBSERVER_AL | RCT_TYPE_OBSERVER_AS)

#define RCT_TYPE_ROOM            0x800U
#define RCT_TYPE_ROOM_MEMBER     0x1000U
#define RCT_TYPE_PRODUCER_EXPORT 0x2000U
#define RCT_TYPE_WORKER_ADAPTER  0x4000U

#define RCT_TYPE_UPPER (RCT_TYPE_WORKER_ADAPTER + 1)

#define RCT_RESOURCE_BASE_FIELDS            \
  uint32_t type;                             \
  wrc_resource_t id;                        \
  char uuid[IW_UUID_STR_LEN + 1];           \
  /* Unsafe fields, resource must be \
     locked before access */                \
  IWPOOL *pool;                             \
  JBL identity;                             \
  int64_t refs;                             \
  char *user_data;                          \
  void (*dispose)(void*);                   \
  void (*close)(void*);                     \
  wrc_resource_t wid;                       \
  bool closed;

#define RCT_TRANSPORT_FIELDS                  \
  RCT_RESOURCE_BASE_FIELDS                    \
  rct_router_t *router;                       \
  struct rct_transport *next;                 \
  struct rct_producer_base *producers;        \
  struct rct_consumer_base *consumers;        \
  const char *cname_for_producers;            \
  JBL_NODE data;                              \
  int stream_max_slots;                       \
  int stream_next_id;                         \
  int next_mid;                               \
  bool *stream_slots;

typedef struct rct_resource_base {
  RCT_RESOURCE_BASE_FIELDS
} rct_resource_base_t;

typedef struct rct_router {
  RCT_RESOURCE_BASE_FIELDS
  wrc_resource_t   worker_id;
  struct rct_room *room;
  struct rct_transport    *transports;
  struct rct_rtp_observer *observers;
  JBL_NODE rtp_capabilities;                // Router RTP capabilities allocated in pool
  bool     close_pending;
} rct_router_t;

typedef struct rct_rtp_observer {
  RCT_RESOURCE_BASE_FIELDS;       // { routerId: uuidv4, rtpObserverId : uuidv4 }
  rct_router_t *router;
  struct rct_rtp_observer *next;
  bool paused;
} rct_rtp_observer_t;

typedef struct rct_transport {
  RCT_TRANSPORT_FIELDS            // { routerId: uuidv4, transportId : uuidv4 }
} rct_transport_t;

typedef struct rct_transport_ip {
  const char *ip;           /**< Listening IPv4 or IPv6. */
  const char *announced_ip; /**< Announced IPv4 or IPv6
                                (useful when running behind NAT with private IP */
  struct rct_transport_ip *next;
} rct_transport_ip_t;

#define RCT_TRN_ENABLE_UDP          0x01
#define RCT_TRN_PREFER_UDP          0x02
#define RCT_TRN_ENABLE_TCP          0x04
#define RCT_TRN_PREFER_TCP          0x08
#define RCT_TRN_ENABLE_SCTP         0x10
#define RCT_TRN_ENABLE_DATA_CHANNEL 0x20
#define RCT_WEBRTC_DEFAULT_FLAGS    (RCT_TRN_ENABLE_UDP | RCT_TRN_PREFER_UDP | RCT_TRN_ENABLE_TCP)

typedef struct rct_transport_webrtc_spec {
  IWPOOL *pool;
  rct_transport_ip_t *ips;
  uint32_t flags;             /**< Default: RCT_TRN_ENABLE_UDP | RCT_TRN_PREFER_UDP | RCT_TRN_ENABLE_TCP */
  struct {
    int outgoing_bitrate;     /**< Default: 600000 */
  } initial;
  struct {
    int max_message_size;     /**< Default: 262144 */
    struct {
      int os;                 /**< Default: 1024 */
      int mis;                /**< Default: 1024 */
    } streams;
  } sctp;
} rct_transport_webrtc_spec_t;

typedef struct rct_transport_webrtc {
  RCT_TRANSPORT_FIELDS
  rct_transport_webrtc_spec_t *spec;
} rct_transport_webrtc_t;

typedef enum {
  RCT_AES_CM_128_HMAC_SHA1_80,
  RCT_AES_CM_128_HMAC_SHA1_32,
} rtc_srtp_crypto_suite_e;

typedef struct rct_transport_plain_spec {
  IWPOOL *pool;

  /// Listening IP address
  rct_transport_ip_t listen_ip;

  /// Do not use RTCP-mux (RTP and RTCP in the same port)
  bool no_mux;

  /// Whether remote IP:port should be auto-detected based on first RTP/RTCP
  /// packet received. If enabled, connect() method must not be called unless
  /// SRTP is enabled. If so, it must be called with just remote SRTP parameters.
  bool comedia;

  /// Create a SCTP association
  bool enable_sctp;

  /// Enable SRTP. For this to work, connect() must be called
  /// with remote SRTP parameters.
  bool enable_srtp;

  /// The SRTP crypto suite to be used if enableSrtp is set.
  /// Default: AES_CM_128_HMAC_SHA1_80
  rtc_srtp_crypto_suite_e crypto_suite;

  struct {
    int max_message_size;     /**< Default: 262144 */
    struct {
      int os;                 /**< Default: 1024 */
      int mis;                /**< Default: 1024 */
    } streams;
  } sctp;
} rct_transport_plain_spec_t;

typedef struct rct_transport_plain {
  RCT_TRANSPORT_FIELDS
  rct_transport_plain_spec_t *spec;
} rct_transport_plain_t;

typedef struct rct_transport_direct {
  RCT_TRANSPORT_FIELDS
  uint32_t max_message_size;
} rct_transport_direct_t;

typedef enum {
  RCT_PRODUCER_SIMPLE,
  RCT_PRODUCER_SIMULCAST,
  RCT_PRODUCER_SVC,
} rct_producer_type_e;

typedef struct rct_producer_spec {
  IWPOOL     *pool;
  const char *uuid;
  const char *cname;
  JBL_NODE    rtp_parameters;
  JBL_NODE    consumable_rtp_parameters;
  wrc_resource_t transport;
  int  rtp_kind;
  int  key_frame_request_delay;
  bool paused;
} rct_producer_spec_t;

#define RCT_PRODUCER_FIELDS             \
  RCT_RESOURCE_BASE_FIELDS              \
  rct_transport_t *transport;           \
  struct rct_consumer_base *consumers;  \
  struct rct_producer_base *next;       \
  bool paused;

typedef struct rct_producer_base {
  RCT_PRODUCER_FIELDS
} rct_producer_base_t;


struct rct_producer_export;

#define RCT_PRODUCER_EXPORT_PARAMS_FIELDS         \
  iwrc (*on_start)(struct rct_producer_export*);  \
  void (*on_pause)(struct rct_producer_export*);  \
  void (*on_resume)(struct rct_producer_export*); \
  void (*on_close)(struct rct_producer_export*);  \
  void *hook_user_data; \
  bool close_on_pause;

typedef struct rct_producer_export_params {
  RCT_PRODUCER_EXPORT_PARAMS_FIELDS
} rct_producer_export_params_t;

typedef struct rct_producer_export {
  RCT_RESOURCE_BASE_FIELDS;
  RCT_PRODUCER_EXPORT_PARAMS_FIELDS;
  struct rct_producer  *producer;
  struct rct_transport *transport;
  struct rct_consumer  *consumer;
  JBL_NODE    codec;
  const char *ip;
  const char *sdp;
  uint16_t    port;
} rct_producer_export_t;

typedef struct rct_producer {
  RCT_PRODUCER_FIELDS
  rct_producer_spec_t *spec;
  const char *producer_type_str;
  // *INDENT-OFF*
  rct_producer_export_t *export;
  // *INDENT-ON*
  rct_producer_type_e producer_type;
} rct_producer_t;

typedef enum {
  RCT_DATA_SCTP,
  RCT_DATA_DIRECT,
} rct_data_type_e;

typedef struct rct_producer_data {
  RCT_PRODUCER_FIELDS
  JBL_NODE    sctp_stream_parameters;
  const char *label;
  const char *protocol;
  rct_data_type_e data_type;
} rct_producer_data_t;

typedef struct rct_producer_data_spec {
  IWPOOL     *pool;
  JBL_NODE    sctp_stream_parameters;
  const char *label;
  const char *protocol;
  const char *uuid;
} rct_producer_data_spec_t;

typedef struct rct_consumer_layer {
  int spartial;
  int temporal;
} rct_consumer_layer_t;

#define RCT_CONSUMER_FIELDS         \
  RCT_RESOURCE_BASE_FIELDS          \
  rct_producer_base_t *producer;    \
  rct_transport_t *transport;       \
  struct rct_consumer_base *next;   \
  struct rct_consumer_base *next_transport;

typedef struct rct_consumer_base {
  RCT_CONSUMER_FIELDS
} rct_consumer_base_t;

typedef struct rct_consumer {
  RCT_CONSUMER_FIELDS
  JBL_NODE rtp_capabilities;
  JBL_NODE rtp_parameters;
  JBL      producer_scores;
  // *INDENT-OFF*
  struct rct_producer_export *export;
  // *INDENT-ON*
  rct_consumer_layer_t preferred_layer;
  rct_consumer_layer_t current_layer;
  int score;
  int producer_score;
  int priority;
  atomic_bool paused;
  bool resume_by_producer; // Unpause consumer when producer will be resumed
} rct_consumer_t;

typedef struct rct_consumer_data {
  RCT_CONSUMER_FIELDS
  JBL_NODE    sctp_stream_parameters;
  const char *label;
  const char *protocol;
  rct_data_type_e data_type;
} rct_consumer_data_t;

///////////////////////////////////////////////////////////////////////////
//                           Conference                                  //
///////////////////////////////////////////////////////////////////////////

struct rct_room_member;

struct rct_resource_ref {
  struct rct_resource_base *b;
  uint32_t flags;
};

#define RCT_ROOM_MEETING 0x01U
#define RCT_ROOM_WEBINAR 0x02U
#define RCT_ROOM_PRIVATE 0x04U
#define RCT_ROOM_ALO     0x08U // Audio level observer enabled
#define RCT_ROOM_ASO     0x10U // Active specaker observer enabled
// Room members leave/join events are not stored into room log
#define RCT_ROOM_LIGHT RCT_ROOM_WEBINAR

typedef struct rct_room {
  RCT_RESOURCE_BASE_FIELDS
  char     cid[IW_UUID_STR_LEN + 1];
  uint64_t cid_ts;                 // Room session start time ms
  rct_router_t *router;
  char *name;
  struct rct_room_member *members;
  int64_t owner_user_id;
  wrc_resource_t active_speaker_member_id;
  uint32_t       flags;
  int  num_recording_sessions;
  bool has_started_recording;
  bool close_pending;
  bool close_pending_task;
#if (ENABLE_WHITEBOARD == 1)
  char *whiteboard_link;       // Link that stores actual link for whiteboard opening
  int   num_whiteboard_clicks; // How many times whiteboard has been opened. On first notify room members
#endif
} rct_room_t;

typedef struct rct_room_member {
  RCT_RESOURCE_BASE_FIELDS
  int64_t  user_id;
  uint32_t wsid;               // WS session id
  char    *name;
  struct rct_room *room;
  void *special;
  struct rct_room_member *next;
  uint32_t flags;
  IWULIST  resource_refs;       // Weak refs linked resources: `struct rct_resource_ref`
  JBL_NODE rtp_capabilities;
} rct_room_member_t;

const char* rct_resource_type_name(int type);

void* rct_resource_close_lk(void *b);

void rct_resource_close(wrc_resource_t resource_id);

void rct_resource_close_of_type(uint32_t resource_type);

void rct_resource_ref_unlock(void *b, bool locked, int nrefs, const char *tag);

void rct_resource_ref_keep_locking(void *b, bool locked, int nrefs, const char *tag);

void* rct_resource_ref_locked(void *b, int nrefs, const char *tag);

void* rct_resource_ref_lk(void *b, int nrefs, const char *tag);

iwrc rct_resource_register_lk(void *b);

iwrc rct_resource_probe_by_uuid(const char *uuid, rct_resource_base_t *b);

iwrc rct_resource_probe_by_id(wrc_resource_t resource_id, rct_resource_base_t *b);

void rct_lock(void);

void rct_unlock(void);

/**
 * @brief Associate a copy of specified data with given resource.
 *
 * @param resource_id Resource identifier
 * @param data Null terminated data string
 */
iwrc rct_set_resource_data(wrc_resource_t resource_id, const char *data);

/**
 * @brief Get copy of data string associated with resourse.
 * @note Caller if responsible to call `free()` on returned data stnring.
 *
 * @param resource_id Resource identifier.
 * @return Pointer to associated data or NULL
 */
char* rct_get_resource_data_copy(wrc_resource_t resource_id);

void* rct_resource_by_uuid_unsafe(const char *resource_uuid, int type);

void* rct_resource_by_id_unsafe(wrc_resource_t resource_id, int type);

void* rct_resource_by_uuid_locked(const char *resource_uuid, int type, const char *tag);

void* rct_resource_by_id_locked(wrc_resource_t resource_id, int type, const char *tag);

void* rct_resource_by_id_unlocked(wrc_resource_t resource_id, int type, const char *tag);

void* rct_resource_by_id_locked_lk(wrc_resource_t resource_id, int type, const char *tag);

void* rct_resource_by_id_locked_exists(wrc_resource_t resource_id, int type, const char *tag);

void rct_resource_unlock_exists(void *b, const char *tag);

void rct_resource_unlock_keep_ref(void *b);

void rct_resource_unlock(void *b, const char *tag);

void rct_resource_get_worker_id_lk(void *b, wrc_resource_t *worker_id_out);

/// @brief Get number of resources of given type.
/// @return `-1` in the case of error.
int rct_resource_get_number_of_type_lk(int type);

iwrc rct_resource_get_identity(
  wrc_resource_t resource_id, int resource_type, JBL *identity_out,
  wrc_resource_t *worker_id_out);

struct rct_json_command_spec {
  wrc_resource_t   resource_id;
  wrc_worker_cmd_e cmd;
  JBL  cmd_data;
  JBL *cmd_out;
  JBL  identity_extra;
  int  resource_type;
  bool async;
};

iwrc rct_resource_json_command2(struct rct_json_command_spec *spec);

iwrc rct_resource_json_command(
  wrc_resource_t   resource_id,
  wrc_worker_cmd_e cmd,
  int              resource_type,
  JBL              cmd_data,
  JBL             *cmd_out);

iwrc rct_resource_json_command_async(
  wrc_resource_t   resource_id,
  wrc_worker_cmd_e cmd,
  int              resource_type,
  JBL              cmd_data);

iwrc rct_resource_json_command_lk_then_unlock(
  void            *b,
  wrc_worker_cmd_e cmd,
  int              resource_type,
  JBL              cmd_data,
  JBL             *cmd_out);

bool rct_codec_is_rtx(JBL_NODE n);

iwrc rct_validate_rtcp_feedback(JBL_NODE n, IWPOOL *pool);

iwrc rct_utils_codecs_is_matched(JBL_NODE ac, JBL_NODE bc, bool strict, bool modify, IWPOOL *pool, bool *res);

iwrc rct_init(void);

void rct_shutdown(void);

void rct_destroy(void);
