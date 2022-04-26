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
#include "gr_watcher.h"
#include "gr_task_worker.h"
#include "gr_db_init.h"
#include "gr_crypt.h"
#include "gr_sentry.h"
#include "grh_routes.h"
#include "grh_session.h"
#include "grh_ws.h"

#include "data_default_router_options.inc"

#include "lic/lic.h"
#include "lic_vars.h"
#include "rct/rct.h"
#include "upd/upd.h"
#include "utils/ini.h"
#include "utils/network.h"
#include "wrc/wrc.h"

#include <iowow/iwutils.h>
#include <iowow/iwconv.h>
#include <iowow/iwp.h>
#include <iwnet/iwn_proc.h>
#include <curl/curl.h>

#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#define GRSTART_FLAG_USE_AUTO_IP             0x01U
#define GRSTART_FLAG_CONFIG_HAS_ANNOUNCED_IP 0x02U
#define GRSTART_FLAG_CONFIG_HAS_STUN_SERVERS 0x04U
#define GRSTART_FLAG_CONFIG_HAS_TURN_SERVERS 0x08U

struct gr_env g_env = {
  .mtx                                = PTHREAD_MUTEX_INITIALIZER,
  .periodic_worker                    = {
    .expire_session_timeout_sec       = -1,
    .expire_guest_session_timeout_sec = -1,
    .expire_ws_ticket_timeout_sec     = -1,
    .dispose_gauges_older_than_sec    = -1
  },
  .log                                = {
    .report_errors                    = 1,
  },
};

static char *_admin_pw;

static void _env_close(void) {
  pthread_mutex_destroy(&g_env.mtx);
  if (g_env.public_overlays) {
    iwhmap_destroy(g_env.public_overlays);
    g_env.public_overlays = 0;
  }
  if (g_env.impl.pool) {
    iwpool_destroy(g_env.impl.pool);
    g_env.impl.pool = 0;
  }
  if (g_env.impl.xstr) {
    iwxstr_destroy(g_env.impl.xstr);
    g_env.impl.xstr = 0;
  }
}

static void usage(const char *err) {
  if (err) {
    printf("\n%s\n", err);
  }
  printf("\nUsage\n\n\t %s [options]\n\n", g_env.program);
  printf("\t-c <cfg>\t\t.ini configuration file\n");
  printf("\t-d <dir>\t\tData files directory\n");
  printf("\t-n <domain>\t\tDomain name used to obtain Let's Encrypt certs\n");
  printf("\t-l <ip>[@<pub ip>]\tListen IP or IP mapping if your server behind NAT\n");
  printf("\t-p <port>\t\tServer network port number\n");
  printf("\t-a <password>\t\tResets password for `admin` account\n");
  printf("\t-s\t\t\tThe server runs behind an HTTPS proxy\n");
  printf("\t-t\t\t\tCleanup a database data before start\n");
  printf("\t-v\t\t\tShow version and license information\n");
  printf("\t-h\t\t\tShow this help message\n");
  printf("\n");
  _env_close();
  exit(1);
}

static void version(void) {
#ifdef GIT_REVISION
  printf("Version: " XSTR(GIT_REVISION));
#else
  printf("Version: dev");
#endif

#ifdef LICENSE_ID
  printf("\nLicense: " LICENSE_ID);
#endif

#ifdef LICENSE_OWNER
  printf("\nOwner: '" LICENSE_OWNER "'");
#endif

#ifdef LICENSE_TERMS
  printf("\nTerms: " LICENSE_TERMS);
#endif

  _env_close();
  exit(0);
}

static const char *config_keys[] = { "{home}", "{cwd}", "{config_file_dir}", "{program}" };

static const char* _replace_config_env(const char *key, void *op) {
  if (!strcmp(key, "{home}")) {
    return getenv("HOME");
  } else if (!strcmp(key, "{cwd}")) {
    return g_env.cwd;
  } else if (!strcmp(key, "{config_file_dir}")) {
    return g_env.config_file_dir;
  } else if (!strcmp(key, "{program}")) {
    return g_env.program;
  }
  return 0;
}

static iwrc _add_server(gr_server_e type, const char *server, int len) {
  // user:password@numb.viagenie.ca
  // stun.l.google.com:19305
  iwrc rc = 0;
  IWPOOL *pool = g_env.impl.pool;
  int i = 0, j = 0, k = 0;
  char *user = 0, *password = 0, *port = 0, *host = 0;
  for (i = len - 2; i > 0; --i) {
    if (server[i] == '@') {
      j = i;
      break;
    }
  }
  if (j > 0) {
    for (i = 0; i < j && server[i] != ':'; ++i);
    user = iwpool_alloc(i + 1, pool);
    RCA(user, finish);
    memcpy(user, server, i);
    user[i] = '\0';
    if (++i < j) {
      password = iwpool_alloc(j - i + 1, pool);
      memcpy(password, server + i, j - i);
      password[j - i] = '\0';
    }
    ++j;
  } else {
    j = 0;
  }
  for (i = len - 2; i > j; --i) {
    if (server[i] == ':') {
      k = i;
      break;
    }
  }
  i = 0;
  if (k > 0) {
    port = iwpool_alloc(len - k, pool);
    RCA(port, finish);
    memcpy(port, server + k + 1, len - k - 1);
    port[len - k - 1] = '\0';
    i = iwatoi(port);
  }
  k = (k > 0 ? k : len) - j;
  host = iwpool_alloc(k + 1, pool);
  RCA(host, finish);
  memcpy(host, server + j, k);
  host[k] = '\0';

  struct gr_server *grs = iwpool_alloc(sizeof(*grs), pool);
  RCA(grs, finish);
  grs->type = type;
  grs->host = host;
  grs->port = i;
  grs->user = user;
  grs->password = password;
  grs->next = 0;
  struct gr_server *s = g_env.servers;
  if (!s) {
    g_env.servers = grs;
  } else {
    for ( ; s->next; s = s->next);
    s->next = grs;
  }

finish:
  return rc;
}

static iwrc _add_servers(gr_server_e type, const char *list) {
  iwrc rc = 0;
  const char *sp = list;
  while (isspace(*sp)) ++sp;
  const char *ep = sp;
  for ( ; *ep != '\0'; ++ep) {
    if (isspace(*ep)) {
      RCC(rc, finish, _add_server(type, sp, (int) (ep - sp)));
      do {
        ++ep;
      } while (isspace(*ep));
      sp = ep;
    }
  }
  if (ep > sp) {
    rc = _add_server(type, sp, (int) (ep - sp));
  }
finish:
  return rc;
}

static void _add_rtc_ports(const char *ports) {
  char sp[32];
  char ep[32];
  int len = (int) strlen(ports);
  if (len > sizeof(sp) - 1) {
    len = sizeof(sp) - 1;
  }
  int i = 0, s = 0, e = 0;
  while (isspace(ports[i])) ++i;
  for ( ; i < len && isdigit(ports[i]); ++i) {
    sp[s++] = ports[i];
  }
  sp[s] = '\0';
  while (i < len && (isspace(ports[i]) || ports[i] == '.')) ++i;
  for ( ; i < len && isdigit(ports[i]); ++i) {
    ep[e++] = ports[i];
  }
  ep[e] = '\0';
  if (s > 0) {
    g_env.rtc.start_port = iwatoi(sp);
  }
  if (e > 0) {
    g_env.rtc.end_port = iwatoi(ep);
  }
}

#define _GR_INI_IN_ROUTER_OPTIONS 1

static iwrc _ini_flush_multiline_section(void) {
  iwrc rc = 0;
  IWPOOL *pool = g_env.impl.pool;
  IWXSTR *xstr = g_env.impl.xstr;
  const char *ptr = iwxstr_ptr(xstr);
  size_t sz = iwxstr_size(xstr);

  if (g_env.impl.ini_pstate == _GR_INI_IN_ROUTER_OPTIONS) {
    if (sz) {
      JBL jbl;
      rc = jbl_from_json(&jbl, ptr);
      if (rc) {
        iwlog_error("Invalid JSON data for key: router_options: %s", ptr);
        iwxstr_clear(xstr);
        return rc;
      }
      IWXSTR *p = iwxstr_new2(iwxstr_size(xstr));
      rc = jbl_as_json(jbl, jbl_xstr_json_printer, p, JBL_PRINT_PRETTY);
      if (!rc) {
        g_env.router_optons_json = iwpool_strdup(pool, iwxstr_ptr(p), &rc);
      }
      iwxstr_destroy(p);
      iwxstr_clear(xstr);
      jbl_destroy(&jbl);
    }
  }
  return rc;
}

static iwrc _http_header(const char *value, IWPOOL *pool) {
  iwrc rc = 0;
  int sz = (int) strlen(value), q = 0;
  char *buf = iwpool_strndup(pool, value, sz, &rc);
  RCRET(rc);
  struct gr_pair *p = iwpool_calloc(sizeof(*p), pool);
  RCA(p, finish);
  for (int i = 0; i < sz; ++i) {
    if ((buf[i] == '\'') || (buf[i] == '"')) {
      switch (++q) {
        case 1:
          p->name = buf + i + 1;
          break;
        case 3:
          p->sv = buf + i + 1;
          break;
        case 2:
        case 4:
          buf[i] = '\0';
          break;
      }
    }
  }
  if (q == 4) {
    if (g_env.http_headers) {
      p->next = g_env.http_headers;
    }
    g_env.http_headers = p;
  }
finish:
  return rc;
}

#define PARSE_BOOL(var) { \
    if (!strcasecmp(value, "true") || !strcasecmp(value, "on") || !strcasecmp(value, "yes")) { \
      var = true; \
    } else if (!strcasecmp(value, "false") || !strcasecmp(value, "off") || !strcasecmp(value, "no")) { \
      var = false; \
    } else { \
      iwlog_error("Config: Wrong [%s] section property %s value", section, name); \
    } \
}

static int _ini_handler(
  void *user, const char *section, const char *name,
  const char *value
  ) {
  iwrc rc = 0;
  IWPOOL *pool = g_env.impl.pool;

  if ((strcmp(name, "router_options") != 0) && (strcmp(name, "access_token") != 0)) {
    iwlog_info("%s:%s=%s", section, name, value);
  }
  if (!strcmp(section, "main")) {
    if (!strcmp(name, "host") || !strcmp(name, "ip")) {
      if (!g_env.host) {
        if (strcmp(value, "auto") == 0) {
          g_env.start_flags |= GRSTART_FLAG_USE_AUTO_IP;
        }
        char *ep = strchr(value, '@');
        if (!ep) {
          g_env.host = iwpool_strdup(pool, value, &rc);
        } else {
          g_env.host = iwpool_strndup(pool, value, ep - value, &rc);
          if (!(g_env.start_flags & GRSTART_FLAG_CONFIG_HAS_ANNOUNCED_IP)) {
            rc = _add_servers(GR_LISTEN_ANNOUNCED_IP_TYPE, value);
            if (!rc) {
              g_env.start_flags |= GRSTART_FLAG_CONFIG_HAS_ANNOUNCED_IP;
            }
          }
        }
      }
    } else if (!strcmp(name, "port")) {
      if (!g_env.port) {
        g_env.port = iwatoi(value);
      }
    } else if (!strcmp(name, "domain_name")) {
      g_env.domain_name = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "https_redirect_port")) {
      if (strcmp("none", value) == 0) {
        g_env.https_redirect_port = -1;
      } else {
        g_env.https_redirect_port = iwatoi(value);
      }
    } else if (!strcmp(name, "data")) {
      g_env.data_dir = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "cert_file")) {
      g_env.cert_file = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "cert_key_file")) {
      g_env.cert_key_file = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "forced_user")) {
      g_env.forced_user = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "initial_data")) {
      int64_t llv = iwatoi(value);
      g_env.initial_data = llv;
    } else if (!strcmp(name, "http_header")) {
      rc = _http_header(value, pool);
    } else if (!strcmp(name, "session_cookies_max_age")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.session_cookies_max_age = llv;
      }
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "private_overlays")) {
    if (!strcmp(name, "roots")) {
      g_env.private_overlays.roots = (void*) iwpool_split_string(pool, value, ",", true);
    } else if (!strcmp(name, "watch")) {
      g_env.private_overlays.watch = (void*) iwpool_split_string(pool, value, ",", true);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "servers")) {
    gr_server_e type = 0;
    if (!strcmp(name, "turn_servers")) {
      type = GR_TURN_SERVER_TYPE;
      rc = _add_servers(type, value);
      if (!rc) {
        g_env.start_flags |= GRSTART_FLAG_CONFIG_HAS_TURN_SERVERS;
      }
    } else if (!strcmp(name, "stun_servers")) {
      type = GR_STUN_SERVER_TYPE;
      g_env.start_flags |= GRSTART_FLAG_CONFIG_HAS_STUN_SERVERS;
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "rtc")) {
    if (!strcmp(name, "ports")) {
      _add_rtc_ports(value);
    } else if (!strcmp(name, "listen_announced_ips")) {
      // Deprecated. Use [main] ip option.
      rc = _add_servers(GR_LISTEN_ANNOUNCED_IP_TYPE, value);
      if (!rc) {
        g_env.start_flags |= GRSTART_FLAG_CONFIG_HAS_ANNOUNCED_IP;
        if (!strcmp(value, "auto")) {
          g_env.start_flags |= GRSTART_FLAG_USE_AUTO_IP;
        }
      }
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "dtls")) {
    if (!strcmp(name, "cert_file")) {
      g_env.dtls.cert_file = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "cert_key_file")) {
      g_env.dtls.cert_key_file = iwpool_strdup(pool, value, &rc);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "log")) {
    if (!strcmp(name, "verbose")) {
      PARSE_BOOL(g_env.log.verbose);
    } else if (!strcmp(name, "report_errors")) {
      PARSE_BOOL(g_env.log.report_errors);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "worker")) {
    if (!strcmp(name, "program")) {
      g_env.worker.program = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "log_level")) {
      g_env.worker.log_level = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "log_tags")) {
      g_env.worker.log_tags = (void*) iwpool_split_string(pool, value, ",;", true);
    } else if (!strcmp(name, "command_timeout_sec")) {
      g_env.worker.command_timeout_sec = iwatoi(value);
    } else if (!strcmp(name, "router_options")) {
      if (g_env.impl.ini_pstate != _GR_INI_IN_ROUTER_OPTIONS) {
        g_env.impl.ini_pstate = _GR_INI_IN_ROUTER_OPTIONS;
        RCC(rc, error, _ini_flush_multiline_section());
      }
      RCC(rc, error, iwxstr_cat2(g_env.impl.xstr, value));
      RCC(rc, error, iwxstr_cat2(g_env.impl.xstr, "\n"));
    } else if (!strcmp(name, "idle_timeout_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.worker.idle_timeout_sec = llv;
      }
    } else if (!strcmp(name, "max_workers")) {
      if (strcmp(value, "auto") != 0) {
        int64_t llv = iwatoi(value);
        if (llv > 0) {
          g_env.worker.max_workers = llv;
        }
      }
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "room")) {
    if (!strcmp(name, "idle_timeout_sec")) {
      g_env.room.idle_timeout_sec = iwatoi(value);
    } else if (!strcmp(name, "max_history_sessions")) {
      g_env.room.max_history_sessions = iwatoi(value);
    } else if (!strcmp(name, "max_history_rooms")) {
      g_env.room.max_history_rooms = iwatoi(value);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "public_overlays")) {
    IWHMAP *m = g_env.public_overlays;
    if (!m) {
      m = iwhmap_create_str(iwhmap_kv_free);
      RCA(m, error);
    }
    g_env.public_overlays = m;
    char *k = strdup(name);
    char *v = strdup(value);
    if (k && v) {
      RCC(rc, error, iwhmap_put(m, k, v));
    } else {
      rc = iwrc_set_errno(IW_ERROR_ALLOC, errno);
      free(k);
      free(v);
      goto error;
    }
  } else if (!strcmp(section, "ws")) {
    if (!strcmp(name, "idle_timeout_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.ws.idle_timeout_sec = (int) llv;
      }
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "periodic_worker")) {
    if (!strcmp(name, "check_timeout_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.periodic_worker.check_timeout_sec = (int) llv;
      }
    } else if (!strcmp(name, "expire_session_timeout_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.periodic_worker.expire_session_timeout_sec = (int) llv;
      }
    } else if (!strcmp(name, "expire_guest_session_timeout_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.periodic_worker.expire_guest_session_timeout_sec = (int) llv;
      }
    } else if (!strcmp(name, "expire_ws_ticket_timeout_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.periodic_worker.expire_ws_ticket_timeout_sec = (int) llv;
      }
    } else if (!strcmp(name, "dispose_gauges_older_than_sec")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.periodic_worker.dispose_gauges_older_than_sec = (int) llv;
      }
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "db")) {
    if (!strcmp(name, "access_token")) {
      g_env.dbparams.access_token = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "access_port")) {
      int64_t llv = iwatoi(value);
      g_env.dbparams.access_port = llv;
    } else if (!strcmp(name, "bind")) {
      g_env.dbparams.bind = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "truncate")) {
      PARSE_BOOL(g_env.dbparams.truncate);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "alo")) {
    if (!strcmp(name, "max_entries")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.alo.max_entries = llv;
      }
    } else if (!strcmp(name, "threshold")) {
      int64_t llv = iwatoi(value);
      if ((llv < 0) && (llv >= -127)) {
        g_env.alo.threshold = llv;
      }
    } else if (!strcmp(name, "interval_ms")) {
      int64_t llv = iwatoi(value);
      if (llv > 100) {
        g_env.alo.interval_ms = llv;
      }
    } else if (!strcmp(name, "disabled")) {
      PARSE_BOOL(g_env.alo.disabled);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "acme")) {
    if (!strcmp(name, "endpoint")) {
      g_env.acme.endpoint = iwpool_strdup(pool, value, &rc);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "recording")) {
    if (!strcmp(name, "ffmpeg")) {
      g_env.recording.ffmpeg = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "verbose")) {
      PARSE_BOOL(g_env.recording.verbose);
    } else if (!strcmp(name, "max_processes")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.recording.max_processes = llv;
      }
    } else if (!strcmp(name, "dir")) {
      g_env.recording.dir = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "nopostproc")) {
      PARSE_BOOL(g_env.recording.nopostproc);
    } else if (!strcmp(name, "nopostproc_wallts")) {
      PARSE_BOOL(g_env.recording.nopostproc_wallts);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "uploads")) {
    if (!strcmp(name, "dir")) {
      g_env.uploads.dir = iwpool_strdup(pool, value, &rc);
    } else if (!strcmp(name, "max_size")) {
      int64_t llv = iwatoi(value);
      if (llv > 0) {
        g_env.uploads.max_size = llv * 1024 * 1024;
      }
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else if (!strcmp(section, "whiteboard")) {
    if (!strcmp(name, "room_data_ttl_sec")) {
      int64_t llv = iwatoi(value);
      if (llv >= 0) {
        g_env.whiteboard.room_data_ttl_sec = (int) llv;
      }
    } else if (!strcmp(name, "public_available")) {
      PARSE_BOOL(g_env.whiteboard.public_available);
    } else {
      iwlog_warn("Config: Unknown [%s] section property %s", section, name);
    }
  } else {
    iwlog_warn("Config: Unknown section: %s", section);
  }

  RCGO(rc, error);
  return 1;

error:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return 0;
}

static void _configure(int argc, char *argv[]) {
  iwrc rc = 0;
  char path[PATH_MAX];

  bool under_proxy = false;
  const char *usage_err = 0;
  IWXSTR *xstr = 0;
  IWPOOL *pool = iwpool_create(512);
  RCA(pool, exit);

  g_env.impl.pool = pool;
  g_env.program = argc ? argv[0] : "";

  RCC(rc, exit, iwp_exec_path(path));
  RCA(g_env.program_file = iwpool_strdup2(pool, path), exit);

  if (!getcwd(path, sizeof(path))) {
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    goto exit;
  }
  RCA(g_env.cwd = iwpool_strdup2(pool, path), exit);
  RCA(g_env.impl.xstr = xstr = iwxstr_new(), exit);

  int ch;
  while ((ch = getopt(argc, argv, "l:p:c:n:d:a:thvs")) != -1) {
    switch (ch) {
      case 'h':
        usage(0);
        break;
      case 'v':
        version();
        break;
      case 't':
        g_env.dbparams.truncate = true;
        break;
      case 'c':
        g_env.config_file = iwpool_strdup(pool, optarg, &rc);
        RCGO(rc, exit);
        break;
      case 'd':
        g_env.data_dir = iwpool_strdup(pool, optarg, &rc);
        RCGO(rc, exit);
        break;
      case 'n':
        g_env.domain_name = iwpool_strdup(pool, optarg, &rc);
        RCGO(rc, exit);
        break;
      case 'p':
        g_env.port = iwatoi(optarg);
        break;
      case 's':
        under_proxy = true;
        break;
      case 'a':
        _admin_pw = strdup(optarg);
        break;
      case 'l': {
        if (strcmp(optarg, "auto") == 0) {
          g_env.start_flags |= GRSTART_FLAG_USE_AUTO_IP;
        }
        char *ep = strchr(optarg, '@');
        if (!ep) {
          g_env.host = iwpool_strdup(pool, optarg, &rc);
        } else {
          g_env.host = iwpool_strndup(pool, optarg, ep - optarg, &rc);
        }
        RCC(rc, exit, _add_servers(GR_LISTEN_ANNOUNCED_IP_TYPE, optarg));
        break;
      }
      default:
        goto usage;
    }
  }
  if (!g_env.data_dir) {
    g_env.data_dir = ".";
  }

  if (!g_env.config_file) {
    static const char *cf[] = { "wirow.ini", "greenrooms.ini" };
    for (int i = 0; i < sizeof(cf) / sizeof(cf[0]); ++i) {
      FILE *f = fopen(cf[i], "r");
      if (f) {
        fclose(f);
        g_env.config_file = cf[i];
        break;
      }
    }
  }

  if (g_env.config_file) {
    g_env.config_file_dir = iwpool_alloc(strlen(g_env.config_file) + 1, pool);
    RCA(g_env.config_file_dir, exit);
    memcpy((char*) g_env.config_file_dir, g_env.config_file, strlen(g_env.config_file) + 1);
    g_env.config_file_dir = dirname((char*) g_env.config_file_dir);

    char *data = iwu_file_read_as_buf(g_env.config_file);
    if (!data) {
      iwlog_error("Failed to load configuration file: %s", g_env.config_file);
      goto exit;
    }
    IWXSTR *buf = 0;
    rc = iwu_replace(&buf, data, (int) strlen(data),
                     config_keys, sizeof(config_keys) / sizeof(config_keys[0]),
                     _replace_config_env, 0);
    free(data);
    RCGO(rc, exit);

    if (ini_parse_string(iwxstr_ptr(buf), _ini_handler, 0)) {
      iwlog_error("Failed to load configuration file: %s", g_env.config_file);
      iwxstr_destroy(buf);
      goto exit;
    }
    iwxstr_destroy(buf);

    RCC(rc, exit, _ini_flush_multiline_section());
  }

  if (!g_env.worker.log_level) {
    g_env.worker.log_level = "warn";
  }

  if (g_env.worker.program) {
    IWP_FILE_STAT fstat;
    rc = iwp_fstat(g_env.worker.program, &fstat);
    if (rc || (fstat.ftype != IWP_TYPE_FILE)) {
      iwlog_error("Config: Worker executable does't exist: %s", g_env.worker.program);
      goto usage;
    }
  }

  if (!(g_env.start_flags & GRSTART_FLAG_CONFIG_HAS_ANNOUNCED_IP)) {
    RCC(rc, exit, _add_servers(GR_LISTEN_ANNOUNCED_IP_TYPE, "auto"));
    g_env.start_flags |= GRSTART_FLAG_USE_AUTO_IP;
  }

  if (!(g_env.start_flags & GRSTART_FLAG_CONFIG_HAS_STUN_SERVERS)) {
    RCC(rc, exit, _add_servers(GR_STUN_SERVER_TYPE,
                               "stun.l.google.com:19305"
                               " stun1.l.google.com:19305"
                               " stun2.l.google.com:19305"));
    g_env.start_flags |= GRSTART_FLAG_CONFIG_HAS_STUN_SERVERS;
  }

  if (!g_env.host) {
    g_env.host = "auto";
    g_env.start_flags |= GRSTART_FLAG_USE_AUTO_IP;
  }
  if (g_env.rtc.start_port < 1) {
    g_env.rtc.start_port = 10000;
  }
  if (g_env.rtc.end_port < 1) {
    g_env.rtc.end_port = 59999;
  }
  if (g_env.rtc.start_port > g_env.rtc.end_port) {
    int tmp = g_env.rtc.start_port;
    g_env.rtc.start_port = g_env.rtc.end_port;
    g_env.rtc.end_port = tmp;
  }
  if (g_env.worker.command_timeout_sec < 0) {
    g_env.worker.command_timeout_sec = 30;
  }
  if (g_env.room.idle_timeout_sec < 1) {
    g_env.room.idle_timeout_sec = 60; // 1 min
  }
  if (g_env.room.max_history_sessions < 1 || g_env.room.max_history_sessions > 1024) {
    g_env.room.max_history_sessions = 16;
  }
  if (g_env.room.max_history_rooms < 1 || g_env.room.max_history_rooms > 1024) {
    g_env.room.max_history_rooms = 255;
  }
  if (g_env.worker.idle_timeout_sec < 1) {
    g_env.worker.idle_timeout_sec = 300; // 5 min
  }
  if (g_env.ws.idle_timeout_sec < 1) {
    g_env.ws.idle_timeout_sec = 60; // 1 min
  }
  if (g_env.periodic_worker.check_timeout_sec < 1) {
    g_env.periodic_worker.check_timeout_sec = 240; // 4 min
  }
  if (g_env.periodic_worker.expire_guest_session_timeout_sec < 0) {
    g_env.periodic_worker.expire_guest_session_timeout_sec = 60 * 60 * 24 * 1; // 1 day
  }
  if (g_env.periodic_worker.expire_session_timeout_sec < 0) {
    g_env.periodic_worker.expire_session_timeout_sec = 60 * 60 * 24 * 30; // 30 days
  }
  if (g_env.periodic_worker.expire_ws_ticket_timeout_sec < 0) {
    g_env.periodic_worker.expire_ws_ticket_timeout_sec = 300; // 5 min
  }
  if (g_env.periodic_worker.dispose_gauges_older_than_sec < 0) {
    g_env.periodic_worker.dispose_gauges_older_than_sec = 60 * 60 * 24 * 31; // 31 days
  }
  if (g_env.dbparams.access_token && (g_env.dbparams.access_port == 0)) {
    g_env.dbparams.access_port = 9191;
  }
  if (g_env.session_cookies_max_age == 0) {
    g_env.session_cookies_max_age = 60 * 60 * 24 * 30;  // 30 days
  }
  if (g_env.alo.interval_ms < 1) {
    g_env.alo.interval_ms = 800;
  }
  if (g_env.alo.max_entries < 1) {
    g_env.alo.max_entries = 16;
  }
  if (g_env.alo.threshold == 0) {
    g_env.alo.threshold = -55;
  }
  if (g_env.log.verbose) {
    iwlog_info("worker:router_options=\n%s", g_env.router_optons_json ?: "");
  }
  if (g_env.worker.max_workers < 1) {
    int cc = iwp_num_cpu_cores();
    if (cc > 6) {
      g_env.worker.max_workers = 6;
    } else if (cc < 3) {
      g_env.worker.max_workers = 2;
    } else {
      g_env.worker.max_workers = cc;
    }
  }
  if (!g_env.acme.endpoint) {
    g_env.acme.endpoint = "https://acme-v02.api.letsencrypt.org/directory";
  }
  if (g_env.recording.max_processes < 1) {
    g_env.recording.max_processes = 32;
  }
  if (!g_env.recording.ffmpeg) {
    g_env.recording.ffmpeg = g_env.program_file;
  }
  if (!g_env.recording.dir) {
    g_env.recording.dir = iwpool_printf(pool, "%s/recordings", g_env.data_dir);
    RCA(g_env.recording.dir, exit);
  }
  if (!g_env.uploads.dir) {
    g_env.uploads.dir = iwpool_printf(pool, "%s/uploads", g_env.data_dir);
    RCA(g_env.uploads.dir, exit);
  }
  if (g_env.uploads.max_size < 1) {
    g_env.uploads.max_size = 50 * 1024 * 1024;
  }
  iwlog_info("Number of workers: %d", g_env.worker.max_workers);

  if (g_env.start_flags & GRSTART_FLAG_USE_AUTO_IP) {
    char *ip = network_detect_best_listen_ip_address();
    if (ip) {
      g_env.auto_ip = iwpool_strdup(g_env.impl.pool, ip, &rc);
      free(ip);
      RCGO(rc, exit);
      iwlog_info("Autodetected external IP address for wirow server: %s", g_env.auto_ip);
    } else {
      iwlog_warn2("Failed to detect network IP address of wirow server");
    }
  }

  if (!g_env.router_optons_json) {
    g_env.router_optons_json = (void*) data_default_router_options;
    if (g_env.log.verbose) {
      iwlog_info("Default router options: %s", g_env.router_optons_json);
    }
  }

  g_env.host = g_env.host ? g_env.host : "localhost";
  if (strcmp(g_env.host, "auto") == 0 && g_env.auto_ip) {
    g_env.host = g_env.auto_ip;
  }

  {
    if (g_env.domain_name || (g_env.cert_file && g_env.cert_key_file)) {
      g_env.ssl_enabled = true;
      if (g_env.https_redirect_port == 0) {
        g_env.https_redirect_port = 80;
      }
    }
    g_env.port = g_env.port > 0 ? g_env.port : (g_env.ssl_enabled ? 443 : 8080);
  }

  if (!under_proxy && !(g_env.domain_name || (g_env.cert_file && g_env.cert_key_file))) {
    usage_err
      = "HTTPS support with real domain name must be enabled for your Wirow server."
        "\n `-n <domain name>` command line option is the easiest way to do that."
        "\n Please check the wirow.pdf manual";
    goto usage;
  }

  return;

usage:
  usage(usage_err);

exit:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  _env_close();
  exit(EXIT_FAILURE);
}

static iwrc _db_open(int argc, char *argv[]) {
  IWXSTR *xp = iwxstr_new();
  if (!xp) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  iwrc rc = ejdb_init();
  RCGO(rc, finish);
  const char *data_dir = g_env.data_dir;
  rc = iwp_mkdirs(data_dir);
  if (rc) {
    iwlog_error("Failed to create directory: %s", data_dir);
    goto finish;
  }

  RCC(rc, finish, iwxstr_printf(xp, "%s/%s", data_dir, "greenrooms.db"));
  FILE *f = fopen(iwxstr_ptr(xp), "r");
  if (f) {
    fclose(f);
  } else {
    iwxstr_clear(xp);
    RCC(rc, finish, iwxstr_printf(xp, "%s/%s", data_dir, "wirow.db"));
  }

  g_env.dbparams.path = iwpool_strdup(g_env.impl.pool, iwxstr_ptr(xp), &rc);
  RCGO(rc, finish);
  iwlog_info("Opening %s", g_env.dbparams.path);

  EJDB_OPTS opts = {
    .kv                        = {
      .path                    = g_env.dbparams.path,
      .oflags                  = g_env.dbparams.truncate ? IWKV_TRUNC : 0,
      .wal                     = {
        .savepoint_timeout_sec = 5,
        .checkpoint_buffer_sz  = 209715200 // 200Mb
      }
    }
  };

  if (g_env.dbparams.access_token && g_env.dbparams.access_port) {
    opts.http.bind = g_env.dbparams.bind;
    opts.http.access_token = g_env.dbparams.access_token;
    opts.http.port = g_env.dbparams.access_port;
    opts.http.blocking = false;
    opts.http.enabled = true;
    iwlog_info("Database WS endpoint: ws://%s:%d", (opts.http.bind ? opts.http.bind : "localhost"), opts.http.port);
  }

  EJDB db;
  RCC(rc, finish, ejdb_open(&opts, &db));
  g_env.db = db;

finish:
  iwxstr_destroy(xp);
  return rc;
}

iwrc gr_app_thread_register(pthread_t thr) {
  iwrc rc = 0;
  pthread_mutex_lock(&g_env.mtx);
  struct gr_thread *gt = malloc(sizeof(*gt));
  RCA(gt, finish);
  gt->next = 0;
  gt->thread = thr;
  if (!g_env.threads) {
    g_env.threads = gt;
  } else {
    struct gr_thread *g = g_env.threads;
    for ( ; g->next; g = g->next);
    g->next = gt;
  }
finish:
  pthread_mutex_unlock(&g_env.mtx);
  return rc;
}

void gr_app_thread_unregister(pthread_t thr) {
  pthread_mutex_lock(&g_env.mtx);
  for (struct gr_thread *t = g_env.threads, *p = 0, *next = 0; t; p = t, t = next) {
    next = t->next;
    if (t->thread == thr) {
      if (p) {
        p->next = next;
      } else {
        g_env.threads = next;
      }
      free(t);
      break;
    }
  }
  pthread_mutex_unlock(&g_env.mtx);
}

void gr_shutdown_hook_add(gr_shutdown_hook_fn hook, void *user_data) {
  pthread_mutex_lock(&g_env.mtx);
  struct gr_shutdown_hook *h = malloc(sizeof(*h));
  if (h) {
    h->next = g_env.shutdown_hooks;
    h->hook = hook;
    h->user_data = user_data;
    g_env.shutdown_hooks = h;
  }
  pthread_mutex_unlock(&g_env.mtx);
}

static void _shutdown_hooks_run(void) {
  struct gr_shutdown_hook *h = g_env.shutdown_hooks;
  while (h) {
    struct gr_shutdown_hook *n = h->next;
    h->hook(h->user_data);
    free(h);
    h = n;
  }
  g_env.shutdown_hooks = 0;
}

static void _app_threads_join() {
  iwrc rc = 0;
  struct gr_thread *g, *g2;
  int rci = pthread_mutex_lock(&g_env.mtx);
  if (rci) {
    rc = iwrc_set_errno(IW_ERROR_THREADING_ERRNO, rci);
    iwlog_ecode_error3(rc);
    return;
  }
  g = g_env.threads;
  g_env.threads = 0;
  pthread_mutex_unlock(&g_env.mtx);
  if (g) {
    iwlog_info2("Waiting for completion of app threads....");
  }
  while (g) {
    uint64_t ts = 0;
    rc = iwp_current_time_ms(&ts, false);
    if (!rc) {
      struct timespec tspec = {
        .tv_sec = ts / 1000 + 10 // Max 10 sec
      };
      int rci = pthread_timedjoin_np(g->thread, 0, &tspec); // NOLINT
      if (rci == ETIMEDOUT) {
        rc = iwrc_set_errno(IW_ERROR_THREADING_ERRNO, rci);
        iwlog_ecode_error(rc, "Joining thread %" PRIx64 " timed out (10s)", (int64_t) g->thread);
      } else if (rci) {
        rc = iwrc_set_errno(IW_ERROR_THREADING_ERRNO, rci);
        iwlog_ecode_error3(rc);
      }
    } else {
      iwlog_ecode_error3(rc);
    }
    g2 = g;
    g = g->next;
    free(g2);
  }
}

void gr_shutdown() {
  if (g_env.poller) {
    iwn_poller_shutdown_request(g_env.poller);
  }
}

static const char* _ecodefn(locale_t locale, uint32_t ecode) {
  if (!((ecode > _GR_ERROR_START) && (ecode < _GR_ERROR_END))) {
    return 0;
  }
  switch (ecode) {
    case GR_ERROR_INVALID_PUBLIC_KEY:
      return "Invalid public key provided (GR_ERROR_INVALID_PUBLIC_KEY)";
    case GR_ERROR_INVALID_JWT:
      return "Invalid JWT received GR_ERROR_INVALID_JWT)";
    case GR_ERROR_INVALID_SIGNATURE:
      return "Invalid signature (GR_ERROR_INVALID_SIGNATURE)";
    case GR_ERROR_JWT_API:
      return "JWT API error (GR_ERROR_JWT_API)";
    case GR_ERROR_HTTP:
      return "HTTP response error or invalid response data (GR_ERROR_HTTP)";
    case GR_ERROR_WORKER_SPAWN:
      return "Failed to spawn worker process (GR_ERROR_WORKER_SPAWN)";
    case GR_ERROR_WORKER_COMM:
      return "Error communicating with worker process (GR_ERROR_WORKER_COMM)";
    case GR_ERROR_RESOURCE_NOT_FOUND:
      return "WRC resource not found (GR_ERROR_RESOURCE_NOT_FOUND)";
    case GR_ERROR_WORKER_COMMAND_TIMEOUT:
      return "Worker command timeout (GR_ERROR_WORKER_COMMAND_TIMEOUT)";
    case GR_ERROR_WORKER_EXIT:
      return "Worker exited (GR_ERROR_WORKER_EXIT)";
    case GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED:
      return "Unexpected data received from worker (GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED)";
    case GR_ERROR_WORKER_ANSWER:
      return "Worker answered to command with error (GR_ERROR_WORKER_ANSWER)";
    case GR_ERROR_PAYLOAD_TOO_LARGE:
      return "Message payload is too large (GR_ERROR_PAYLOD_TOO_LARGE)";
    case GR_ERROR_UNKNOWN_TICKET_ID:
      return "Unknown ticket ID (GR_ERROR_UNKNOWN_TICKET_ID)";
    case GR_ERROR_COMMAND_INVALID_INPUT:
      return "Invalid command input (GR_ERROR_COMMAND_INVALID_INPUT)";
    case GR_ERROR_INVALID_DATA_RECEIVED:
      return "Invalid data received. (GR_ERROR_INVALID_DATA_RECEIVED)";
    case GR_ERROR_MEMBER_IS_JOINED_ALREADY:
      return "Member is joined already (GR_ERROR_MEMBER_IS_JOINED_ALREADY)";
    case GR_ERROR_CURL_API_FAIL:
      return "cURL API call fail (GR_ERROR_CURL_API_FAIL)";
    case GR_ERROR_CRYPTO_API_FAIL:
      return "Crypto API call fail (GR_ERROR_CRYPTO_API_FAIL)";
    case GR_ERROR_ACME_API_FAIL:
      return "ACME endpoint API remote call fail (GR_ERROR_ACME_API_FAIL)";
    case GR_ERROR_DECRYPT_FAILED:
      return "Data cannot be decrypted (GR_ERROR_DECRYPT_FAILED)";
    case GR_ERROR_LICENSE:
      return "Failed to check license or license is invalid or expired (GR_ERROR_LICENSE)";
    case GR_ERROR_ACME_CHALLENGE_FAIL:
      return "ACME challenge or authorization fail (GR_ERROR_ACME_CHALLENGE_FAIL)";
    case GR_ERROR_MEDIA_PROCESSING:
      return "Media file processing error (GR_ERROR_MEDIA_PROCESSING)";
  }
  return 0;
}

iwrc gr_shutdown_noweb(void) {
  if (!__sync_bool_compare_and_swap(&g_env.shutdown, false, true)) {
    return 0;
  }
  iwrc rc = 0;
  iwlog_info2("Waiting for child processes to be terminated...");
  wrc_shutdown();
  iwn_proc_kill_all(SIGTERM);
  iwn_proc_wait_all();
  iwlog_info("All child processes terminated");
  _shutdown_hooks_run();
  rct_shutdown();
  iwn_poller_shutdown_request(g_env.poller);
  _app_threads_join();
  IWRC(iwtp_shutdown(&g_env.tp, true), rc);
  IWRC(iwstw_shutdown(&g_env.stw, true), rc);
  iwn_poller_destroy(&g_env.poller);
  rct_close();
  wrc_close();
  grh_ws_close();
  iwn_wf_destroy(g_env.wf);
  iwn_wf_destroy(g_env.wf80);
  iwn_proc_dispose();
  ejdb_close(&g_env.db);
  _env_close();
  curl_global_cleanup();
  return rc;
}

static void _on_signal(int signo) {
  static int sig_cnt;
  ++sig_cnt;
  if (sig_cnt == 1) {
    gr_shutdown();
  } else if (sig_cnt == 2) {
    iwlog_warn("Next SIGINT/SIGTERM will kill wirow by SIGKILL...");
  } else if (sig_cnt >= 3) {
    iwlog_warn("Killing wirow by SIGKILL");
    raise(SIGKILL);
  }
}

iwrc gr_init_noweb(int argc, char *argv[]) {
  if (signal(SIGTERM, _on_signal) == SIG_ERR) {
    return IW_ERROR_FAIL;
  }
  if (signal(SIGINT, _on_signal) == SIG_ERR) {
    return IW_ERROR_FAIL;
  }

  iwrc rc = iw_init();
  RCGO(rc, finish);

  CURLcode cc = curl_global_init(CURL_GLOBAL_ALL);
  if (cc) {
    iwlog_error("%s", curl_easy_strerror(cc));
    rc = GR_ERROR_CURL_API_FAIL;
    goto finish;
  }

  uint64_t ts;
  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  ts = IW_SWAB64(ts);
  ts >>= 32;
  iwu_rand_seed(ts);

  RCC(rc, finish, iwlog_register_ecodefn(_ecodefn));

  // Parse command line options and do global configuration
  _configure(argc, argv);

  RCC(rc, finish, _db_open(argc, argv));
  RCC(rc, finish, gr_db_init());

  if (_admin_pw) {
    RCC(rc, finish, gr_db_user_create_or_update_pw("admin", _admin_pw, 0, 0));
    free(_admin_pw);
    _admin_pw = 0;
  }

  RCC(rc, finish, iwn_poller_create(8, 4, &g_env.poller));
  RCC(rc, finish, iwstw_start("grstw", 10000, false, &g_env.stw));
  RCC(rc, finish, iwtp_start("grtp-", 8, 10000, &g_env.tp));
  RCC(rc, finish, wrc_init());
  RCC(rc, finish, rct_init());
  RCC(rc, finish, gr_watcher_init());

finish:
  if (rc) {
    gr_shutdown_noweb();
  }
  return rc;
}

static iwrc _server_listen(void) {
  assert(g_env.wf);

  iwrc rc = 0;
  struct iwn_wf_server_spec ss = {
    .listen                = g_env.host,
    .port                  = g_env.port,
    .poller                = g_env.poller,
    .request_file_max_size = g_env.uploads.max_size
  };

  iwlog_info("Server on: %s:%d", g_env.host, g_env.port);

  if (g_env.ssl_enabled) {
    ss.ssl.certs = g_env.cert_file;
    ss.ssl.certs_len = g_env.cert_file ? -1 : 0;
    ss.ssl.private_key = g_env.cert_key_file;
    ss.ssl.private_key_len = g_env.cert_key_file ? -1 : 0;
  }

  RCC(rc, finish, grh_session_store_create(&ss.session_store));
  RCC(rc, finish, iwn_wf_server(&ss, g_env.wf));

  if (g_env.wf80 && g_env.ssl_enabled && g_env.https_redirect_port > 0) {
    iwlog_info("HTTP/HTTPS redirect port: %d", g_env.https_redirect_port);
    ss.port = g_env.https_redirect_port;
    ss.ssl.certs = 0;
    ss.ssl.certs_len = 0;
    ss.ssl.private_key = 0;
    ss.ssl.private_key_len = 0;
    RCC(rc, finish, iwn_wf_server(&ss, g_env.wf80));
  }

finish:
  return rc;
}

int exec_worker(int argc, char **argv);
int exec_ffmpeg(int argc, char **argv);

bool gr_exec_embedded(int argc, char *argv[], int *out_rv) {
  if (argc > 1) {
    if (strcmp("w", argv[1]) == 0) {
      argv[1] = argv[0];
      *out_rv = exec_worker(argc - 1, &argv[1]);
      return true;
    } else if (strcmp("f", argv[1]) == 0) {
      argv[1] = argv[0];
      *out_rv = exec_ffmpeg(argc - 1, &argv[1]);
      return true;
    }
  }
  return false;
}

#ifdef IW_EXEC

int main(int argc, char **argv) {
  umask(0077);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  signal(SIGALRM, SIG_IGN);
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);


  int rv = 0;
  bool shutdown = false;
  if (gr_exec_embedded(argc, argv, &rv)) {
    return rv;
  }
  iwrc rc = RCR(gr_init_noweb(argc, argv));

  RCC(rc, finish, gr_sentry_init(argc, argv));

  RCC(rc, finish, grh_routes_configure());
  RCC(rc, finish, _server_listen());

  RCC(rc, finish, gr_periodic_worker_init());
  RCC(rc, finish, gr_lic_init());

  iwn_poller_poll(g_env.poller);

  IWRC(gr_shutdown_noweb(), rc);
  shutdown = true;

  IWRC(gr_upd_apply(), rc);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    if (!shutdown) {
      gr_shutdown_noweb();
    }
  }
  gr_sentry_dispose();
  iwlog_info2("Goodbye!");
  return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
