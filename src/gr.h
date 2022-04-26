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

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <pthread.h>

#include <iowow/iwlog.h>
#include <iowow/iwpool.h>
#include <iowow/iwhmap.h>
#include <iowow/iwstw.h>
#include <iowow/iwtp.h>
#include <iwnet/iwn_wf.h>

#include <ejdb2/ejdb2.h>

#define NUMBUSZ 64

#define XSTR(s) STR(s)
#define STR(s)  #s

typedef enum {
  _GR_ERROR_START = (IW_ERROR_START + 100000UL),
  GR_ERROR_INVALID_PUBLIC_KEY,              /**< Invalid public key provided (GR_ERROR_INVALID_PUBLIC_KEY) */
  GR_ERROR_INVALID_JWT,                     /**< Invalid JWT received GR_ERROR_INVALID_JWT) */
  GR_ERROR_INVALID_SIGNATURE,               /**< Invalid signature (GR_ERROR_INVALID_SIGNATURE) */
  GR_ERROR_JWT_API,                         /**< JWT API error (GR_ERROR_JWT_API) */
  GR_ERROR_HTTP,                            /**< HTTP response error or invalid response data (GR_ERROR_HTTP) */
  GR_ERROR_RESOURCE_NOT_FOUND,              /**< WRC resource not found (GR_ERROR_RESOURCE_NOT_FOUND) */
  GR_ERROR_WORKER_SPAWN,                    /**< Failed to spawn worker process (GR_ERROR_WORKER_SPAWN) */
  GR_ERROR_WORKER_COMM,                     /**< Error communicating with worker process (GR_ERROR_WORKER_COMM) */
  GR_ERROR_WORKER_COMMAND_TIMEOUT,          /**< WRC Worker command timeout (GR_ERROR_WORKER_COMMAND_TIMEOUT) */
  GR_ERROR_WORKER_EXIT,                     /**< WRC Worker exited (GR_ERROR_WORKER_EXIT) */
  GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED,
  /**< WRC Unexpected data received from worker
     (GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED) */
  GR_ERROR_WORKER_ANSWER,                   /**< WRC Worker answered to command with error (GR_ERROR_WORKER_ANSWER) */
  GR_ERROR_PAYLOAD_TOO_LARGE,               /**< Messasge payload too large */
  GR_ERROR_UNKNOWN_TICKET_ID,               /**< Unknown ticket ID (GR_ERROR_UNKNOWN_TICKET_ID) */
  GR_ERROR_COMMAND_INVALID_INPUT,           /**< Invalid command input (GR_ERROR_COMMAND_INVALID_INPUT) */
  GR_ERROR_INVALID_DATA_RECEIVED,           /**< Invalid data received. (GR_ERROR_INVALID_DATA_RECEIVED) */
  GR_ERROR_MEMBER_IS_JOINED_ALREADY,        /**< Member is joined already (GR_ERROR_MEMBER_IS_JOINED_ALREADY) */
  GR_ERROR_CURL_API_FAIL,                   /**< cURL API call fail (GR_ERROR_CURL_API_FAIL) */
  GR_ERROR_CRYPTO_API_FAIL,                 /**< Crypto API call fail (GR_ERROR_CRYPTO_API_FAIL) */
  GR_ERROR_DECRYPT_FAILED,                  /**< Data cannot be decrypted  (GR_ERROR_DECRYPT_FAILED) */
  GR_ERROR_ACME_API_FAIL,                   /**< ACME endpoint API remote call fail (GR_ERROR_ACME_API_FAIL) */
  GR_ERROR_ACME_CHALLENGE_FAIL,             /**< ACME challenge or authorization fail (GR_ERROR_ACME_CHALLENGE_FAIL) */
  GR_ERROR_LICENSE,
  /**< Failed to check license or license is invalid or expired
     (GR_ERROR_LICENSE) */
  GR_ERROR_MEDIA_PROCESSING,                /**< Media file processing error (GR_ERROR_MEDIA_PROCESSING) */
  _GR_ERROR_END,
} gr_ecode_t;

typedef enum {
  GR_UNKNOWN_SERVER_TYPE,
  GR_STUN_SERVER_TYPE,
  GR_TURN_SERVER_TYPE,
  GR_LISTEN_ANNOUNCED_IP_TYPE,
} gr_server_e;

struct gr_server {
  gr_server_e type;
  const char *host;
  int   port;
  char *user;
  char *password;
  struct gr_server *next;
};

struct gr_thread {
  pthread_t thread;
  struct gr_thread *next;
};

typedef void (*gr_shutdown_hook_fn)(void*);

struct gr_shutdown_hook {
  gr_shutdown_hook_fn hook;
  void *user_data;
  struct gr_shutdown_hook *next;
};

/**
 * @brief Global app configation, services and shared state.
 *
 * Provides the following global services:
 * - db  Database
 * - stw Single thread executor
 * - tp  Thread pool
 */
struct gr_env {
  struct iwn_wf_ctx       *wf;
  struct iwn_wf_ctx       *wf80;
  struct iwn_poller       *poller;
  struct gr_shutdown_hook *shutdown_hooks;

  const char *auto_ip;    /**< Autodetcted IP address. */
  const char *program;
  const char *program_file;
  const char *cwd;
  const char *config_file;
  const char *config_file_dir;
  const char *data_dir;
  const char *host;
  const char *domain_name;    /**< DNS domain name for wirow server */
  const char *cert_file;
  const char *cert_key_file;
  const char *forced_user;
  const char *router_optons_json;
  int  port;
  int  https_redirect_port;
  bool ssl_enabled;

  int session_cookies_max_age; /**< Max age of sessions cookies in seconds Default 1 Month */
  pthread_mutex_t mtx;         /**< Global app mutex */
  struct {
    const char **roots;
    const char **watch;
  } private_overlays;
  IWHMAP *public_overlays; // Resources overlays
  struct {
    int max_workers;      /**< Max number of active workers */
    int idle_timeout_sec; /**< Timeout in seconds when idle worker should shut-down */
    const char  *program;
    const char  *log_level;
    const char **log_tags;
    int command_timeout_sec;
  } worker;
  struct {
    bool verbose;
    bool report_errors; /**< If true backend and frontend errors will be sent to sentry server, default true */
  } log;
  struct {
    int start_port;
    int end_port;
  } rtc;
  struct {
    const char *cert_file;
    const char *cert_key_file;
  } dtls;
  struct {
    int idle_timeout_sec;
    int max_history_sessions; /**< Max number of previous room sessions shown to user */
    int max_history_rooms;    /**< Max number of previous rooms shown to user. */
  } room;
  struct {
    int idle_timeout_sec;  /**< Websocket idle connection timeout seconds  */
  } ws;
  struct {
    int check_timeout_sec;                /**< Periodic worker checks timeout in seconds. */
    int expire_session_timeout_sec;       /**< Number of seconds when idle sessions will be removed */
    int expire_guest_session_timeout_sec; /**< Number of seconds when idle guest sessions will be removed */
    int expire_ws_ticket_timeout_sec;     /**< Number of seconds when ws ticket will be expired */
    int dispose_gauges_older_than_sec;    /**< Dispose gauges older than sec */
  } periodic_worker;
  struct {
    const char *path;
    const char *bind;
    const char *access_token;
    int  access_port;
    bool access_enabled;
    bool truncate;
  } dbparams;
  /// Audio level observer
  struct {
    int max_entries;  /**< Maximum number of entries in the 'volumes”' event. Default 16. */
    int threshold;    /**< Minimum average volume (in dBvo from -127 to 0) for entries in the 'volumes' event. Default
                         -55.*/
    int  interval_ms; /*< Interval in ms for checking audio volumes. Default 800. */
    bool disabled;
  } alo;
  /// Active speaker observer
  struct {
    int  interval_ms; /*< Interval in ms for checking active speacker event. Default: 300. */
    bool disabled;
  } aso;
  struct {
    const char *endpoint;                /**< ACME service directory endpoint */
  } acme;
  struct {
    const char *dir;        /**< Directory to store room recordings */
    const char *ffmpeg;     /**< Path to ffmpeg executable */
    int  max_processes;     /**< Max number of recording processes */
    bool verbose;           /**< Print debug recording messages */
    bool nopostproc;        /**< Recording postprocessing disabled */
    bool nopostproc_wallts; /**< Do not take into account absoluta wall time stamp (PTS) or recorded videos*/
  } recording;
  struct {
    const char *dir;        /**< Directory to store uploaded files */
    size_t      max_size;   /**< Max uploaded file size */
  } uploads;
  struct {
    int  room_data_ttl_sec; /**< Number of seconds when stored whiteboard data will be removed. <= 0 is disabled */
    bool public_available;  /**< If true not participants will have readonly access */
  } whiteboard;
  struct gr_pair   *http_headers;
  struct gr_server *servers;
  struct gr_thread *threads;  /**< App global threads. Mutex protected */
  volatile bool     shutdown; /**< True if we are in shutdown pending state */
  EJDB  db;
  int   initial_data;
  int   start_flags;   /**< Various flags set during app startup. */
  IWSTW stw;           /**< Global single thread executor */
  IWTP  tp;            /**< Global thread pool */
  struct {
    IWPOOL *pool;
    IWXSTR *xstr;
    int     ini_pstate;
  } impl;
};

typedef struct gr_pair {
  jbl_type_t type;
  char      *name;
  struct gr_pair *next;
  union {
    int64_t iv;
    bool    bv;
    char   *sv;
  };
} gr_pair_t;


iwrc gr_app_thread_register(pthread_t thr);

void gr_app_thread_unregister(pthread_t thr);

void gr_shutdown(void);

iwrc gr_shutdown_noweb(void);

iwrc gr_init_noweb(int argc, char *argv[]);

void gr_shutdown_hook_add(gr_shutdown_hook_fn hook, void *user_data);

bool gr_exec_embedded(int argc, char *argv[], int *out_rv);
