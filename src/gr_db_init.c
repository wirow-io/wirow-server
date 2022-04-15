#include "gr_db_init.h"
#include "gr_crypt.h"
#include "lic_vars.h"
#include "lic_env.h"

#include <ejdb2/ejdb2.h>
#include <ejdb2/iowow/iwuuid.h>
#include <ejdb2/iowow/iwp.h>

#include <assert.h>
#include <string.h>

extern struct gr_env g_env;

static iwrc _db_init(void) {
  EJDB db = g_env.db;
  JBL jbl = 0;
  assert(db);
  iwrc rc = ejdb_ensure_index(db, "sessions", "/__id__", EJDB_IDX_UNIQUE | EJDB_IDX_STR);
  IWRC(ejdb_ensure_index(db, "sessions", "/__ts__", EJDB_IDX_I64), rc);
  IWRC(ejdb_ensure_index(db, "users", "/name", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "users", "/uuid", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "users", "/keywords", EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "users", "/ctime", EJDB_IDX_I64), rc);
  IWRC(ejdb_ensure_index(db, "tickets", "/name", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "rooms", "/uuid", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "rooms", "/cid", EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "tasks", "/hook", EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "joins", "/k", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "files", "/uuid", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);
  IWRC(ejdb_ensure_index(db, "gauges", "/t", EJDB_IDX_I64), rc);
  IWRC(ejdb_ensure_index(db, "whiteboards", "/cid", EJDB_IDX_UNIQUE | EJDB_IDX_STR), rc);

  rc = ejdb_get(db, "meta", 1, &jbl);
  if (rc == IW_ERROR_NOT_EXISTS) {
    RCC(rc, finish, jbl_create_empty_object(&jbl));
    RCC(rc, finish, jbl_set_int64(jbl, "__initial__", 0));
    RCC(rc, finish, ejdb_put(db, "meta", jbl, 1));
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _system_user_add(const char *name, const char *pw, int64_t salt, char *pwh, bool hidden) {
  int64_t llv;
  uint64_t ctime;
  JBL jbl = 0, jbl2 = 0;
  char buf[65];

  iwrc rc = RCR(iwp_current_time_ms(&ctime, false));

  char uuid[IW_UUID_STR_LEN + 1];
  iwu_uuid4_fill(uuid);
  uuid[IW_UUID_STR_LEN] = '\0';

  if (!pwh) {
    pwh = buf;
    salt = gr_crypt_pwhash(pw, strlen(pw), 0, pwh);
  }

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "name", name));
  RCC(rc, finish, jbl_set_string(jbl, "uuid", uuid));
  RCC(rc, finish, jbl_set_int64(jbl, "ctime", ctime));
  RCC(rc, finish, jbl_set_bool(jbl, "guest", false));
  RCC(rc, finish, jbl_set_int64(jbl, "salt", salt));
  RCC(rc, finish, jbl_set_string(jbl, "pwh", pwh));
  RCC(rc, finish, jbl_set_string(jbl, "perms", "admin,user"));
  if (!hidden) {
    RCC(rc, finish, jbl_set_bool(jbl, "removed", false));
    RCC(rc, finish, jbl_create_empty_array(&jbl2));
    RCC(rc, finish, jbl_set_string(jbl2, 0, name));
    RCC(rc, finish, jbl_set_nested(jbl, "keywords", jbl2));
  }

  RCC(rc, finish, ejdb_put_new(g_env.db, "users", jbl, &llv));

finish:
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc;
}

iwrc gr_db_user_create(const char *name, const char *pw, int64_t salt, char *pwh, bool hidden) {
  return _system_user_add(name, pw, salt, pwh, hidden);
}

static iwrc _apply_stage_3(void) {
  return ejdb_remove_collection(g_env.db, "sessions");
}

static iwrc _stages_apply(void) {
  int64_t llv;
  JBL jbl = 0;
  EJDB db = g_env.db;
  iwrc rc = ejdb_get(db, "meta", 1, &jbl);
  RCGO(rc, finish);
  RCC(rc, finish, jbl_object_get_i64(jbl, "__initial__", &llv));
  if (llv < 3) { // Starting from 3 because of legacy installations
    RCC(rc, finish, _apply_stage_3());
    RCC(rc, finish, ejdb_merge_or_put(db, "meta", "{\"__initial__\":3}", 1));
    // llv = 3;
  }
finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _users_apply(void) {
  iwrc rc = 0;
  JQL q = 0;

#ifdef LICENSE_ADM_PWH
  {
    int64_t llv;
    RCC(rc, finish, jql_create(&q, "users", "/[name = :?]"));
    RCC(rc, finish, jql_set_str(q, 0, 0, LICENSE_LOGIN));
    RCC(rc, finish, ejdb_count(g_env.db, q, &llv, 0));
    if (llv == 0) {
      RCC(rc, finish, _system_user_add(LICENSE_LOGIN, 0, LICENSE_ADM_PWS, LICENSE_ADM_PWH, false));
    }
  }
#endif

#if (CREATE_DEV_USER == 1)
  {
    int64_t llv;
    RCC(rc, finish, ejdb_count2(g_env.db, "users", "/[name = dev]", &llv, 0));
    if (llv == 0) {
      RCC(rc, finish, _system_user_add("dev", "grStart011", 0, 0, true));
    }
    RCC(rc, finish, ejdb_count2(g_env.db, "users", "/[name = dev2]", &llv, 0));
    if (llv == 0) {
      RCC(rc, finish, _system_user_add("dev2", "grStart011", 0, 0, true));
    }
    RCC(rc, finish, ejdb_count2(g_env.db, "users", "/[name = dev3]", &llv, 0));
    if (llv == 0) {
      RCC(rc, finish, _system_user_add("dev3", "grStart011", 0, 0, true));
    }
  }
#endif

#if defined(LICENSE_ADM_PWH) || (CREATE_DEV_USER == 1)
finish:
#endif
  jql_destroy(&q);
  return rc;
}

iwrc gr_db_init(void) {
  iwrc rc = RCR(_db_init());
  rc = RCR(_users_apply());
  if (g_env.initial_data >= 1) {
    rc = _stages_apply();
  }
  return rc;
}
