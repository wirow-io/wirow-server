#include "grh_adm_users.h"
#include "gr_crypt.h"
#include "utils/utf8.h"
#include "lic_env.h"

#include <iowow/iwuuid.h>
#include <iowow/iwpool.h>
#include <iowow/iwconv.h>
#include <ejdb2/jql.h>
#include <ejdb2/ejdb2.h>

#include <string.h>

static int _cmp_permissions(const void *v1, const void *v2) {
  return strcmp(v1, v2);
}

static const char* _normalize_permissions(const char *plist, IWPOOL *pool) {
  // todo: improve it: deduplication, perm autoset
  if (!plist || (*plist == '\0')) {
    return "";
  }
  const char **splits = iwpool_split_string(pool, plist, ",", true);
  if (!splits) {
    return 0;
  }
  int cnt = 0, sz = 0;
  for (const char **s = splits; *s; ++s) {
    ++cnt;
    sz += (int) strlen(*s) + 1;
  }
  if (cnt < 2) {
    return *splits;
  }
  qsort(splits, cnt, sizeof(char*), _cmp_permissions);
  char *ret = iwpool_alloc(sz, pool);
  if (ret) { // Now join it again
    char *p = ret;
    for (const char **s = splits; *s; ++s, --cnt) {
      size_t l = strlen(*s);
      memcpy(p, *s, l);
      if (cnt == 1) {
        p[l] = '\0';
        break;
      } else {
        p[l] = ',';
        p += l + 1;
      }
    }
  }
  return ret;
}

static int _list(struct iwn_wf_req *req, void *d) {
  iwrc rc = 0;
  JQL q = 0;
  JBL_NODE data = 0, n;
  EJDB_DOC doc = 0;
  int ret = 500;
  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  struct iwn_val val = iwn_pair_find_val(&req->query_params, "skip", IW_LLEN("skip"));
  int64_t skip = val.len ? iwatoi(val.buf) : 0;

  val = iwn_pair_find_val(&req->query_params, "limit", IW_LLEN("limit"));
  int64_t limit = val.len ? iwatoi(val.buf) : 1000;

  val = iwn_pair_find_val(&req->query_params, "query", IW_LLEN("query"));
  char *query = val.buf;
  if ((limit <= 0) || (skip < 0) || (limit > 1000) || (query && (val.len > 100))) {
    ret = 400;
    goto finish;
  }
  if (query) {
    utf8lwr(query);
  } else {
    query = "";
  }
  RCC(rc, finish, jql_create(&q, "users", "/keywords/[** ~ :?] and /[removed = false]"
                             " | /{ctime,name,email,uuid,perms,notes}"
                             " | skip :? limit :? desc /ctime"));
  RCC(rc, finish, jql_set_str(q, 0, 0, query));
  RCC(rc, finish, jql_set_i64(q, 0, 1, skip));
  RCC(rc, finish, jql_set_i64(q, 0, 2, limit));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 0, pool));

  RCC(rc, finish, jbn_from_json("{\"users\": [], \"totalNumber\": 0}", &data, pool));

  if (!jbn_at(data, "/users", &n) && n->type == JBV_ARRAY) {
    for ( ; doc; doc = doc->next) {
      jbn_add_item(n, doc->node);
    }
  }

  if (!jbn_at(data, "/totalNumber", &n) && n->type == JBV_I64) {
    RCC(rc, finish, ejdb_count2(g_env.db, "users", "/[removed = false]", &n->vi64, 0));
  }

  ret = iwn_http_response_jbn(req->http, data, 200, true);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
  return ret;
}

static int _update(struct iwn_wf_req *req, void *d) {
  iwrc rc = 0;
  int ret = 500;
  JQL q = 0;
  int64_t llv = 0;
  JBL_NODE user = 0, n;
  const char *error = 0;
  IWPOOL *pool = 0;
  char uuid[IW_UUID_STR_LEN + 1], pwh[65];

#ifdef LIMIT_REGISTERED_USERS
  RCC(rc, finish, ejdb_count2(g_env.db, "users", "/[removed = false]", &llv, 0));
  if (llv >= LIMIT_REGISTERED_USERS) {
    ocs = OCS_INTERNAL_ERROR;
    goto finish;
  }
#endif

  RCA(pool = iwpool_create_empty(), finish);

  struct iwn_val vid = iwn_pair_find_val(&req->form_params, "uuid", IW_LLEN("uuid"));
  struct iwn_val vname = iwn_pair_find_val(&req->form_params, "name", IW_LLEN("name"));
  struct iwn_val vemail = iwn_pair_find_val(&req->form_params, "email", IW_LLEN("email"));
  struct iwn_val vpassword = iwn_pair_find_val(&req->form_params, "password", IW_LLEN("password"));
  struct iwn_val vperms = iwn_pair_find_val(&req->form_params, "perms", IW_LLEN("perms"));
  struct iwn_val vnotes = iwn_pair_find_val(&req->form_params, "notes", IW_LLEN("notes"));

  const char *id = vid.len ? iwpool_strndup2(pool, vid.buf, vid.len) : 0;
  const char *perms = vperms.len ? iwpool_strndup2(pool, vperms.buf, vperms.len) : 0;
  char *email = vemail.len ? iwpool_strndup2(pool, vemail.buf, vemail.len) : 0;

  if (  (vname.len < 2) || (vname.len > 64)
     || (id && !iwu_uuid_valid(id))
     || (vnotes.len > 1024)
     || (vpassword.len > 255)
     || (vperms.len > 255)
     || (vemail.len > 255)) {
    ret = 400;
    goto finish;
  }

  if (!id) {
    if (vpassword.len == 0) { // UUID is not set as well as password
      ret = 400;
      goto finish;
    }
    iwu_uuid4_fill(uuid);
    uuid[IW_UUID_STR_LEN] = '\0';
    id = uuid;
    RCC(rc, finish, jql_create(&q, "users", "/[name = :?] and /[removed = false]"));
    RCC(rc, finish, jql_set_str3(q, 0, 0, vname.buf, vname.len));
    RCC(rc, finish, ejdb_count(g_env.db, q, &llv, 1));
    if (llv > 0) {
      error = "Users.error_exists";
      goto finish;
    }
  }

  const char *nperms = _normalize_permissions(perms, pool);
  if (!nperms) {
    goto finish;
  }

  uint64_t ctime;
  RCC(rc, finish, iwp_current_time_ms(&ctime, false));

  RCC(rc, finish, jbn_from_json("{}", &user, pool));
  RCC(rc, finish, jbn_add_item_str(user, "name", vname.buf, vname.len, 0, pool));
  RCC(rc, finish, jbn_add_item_str(user, "uuid", id, IW_UUID_STR_LEN, 0, pool));
  if (vemail.len) {
    RCC(rc, finish, jbn_add_item_str(user, "email", vemail.buf, vemail.len, 0, pool));
  }
  RCC(rc, finish, jbn_add_item_i64(user, "ctime", ctime, 0, pool));
  RCC(rc, finish, jbn_add_item_str(user, "perms", nperms, -1, 0, pool));
  RCC(rc, finish, jbn_add_item_str(user, "notes", vnotes.buf, vnotes.len, 0, pool));
  RCC(rc, finish, jbn_add_item_bool(user, "removed", false, 0, pool));
  if (vpassword.len) {
    RCC(rc, finish, jbn_add_item_i64(user, "salt", gr_crypt_pwhash(vpassword.buf, vpassword.len, 0, pwh), 0, pool));
    RCC(rc, finish, jbn_add_item_str(user, "pwh", pwh, -1, 0, pool));
  }
  RCC(rc, finish, jbn_add_item_arr(user, "keywords", &n, pool));
  {
    char *lname = iwpool_strndup(pool, vname.buf, vname.len, &rc);
    RCGO(rc, finish);
    utf8lwr(lname);
    RCC(rc, finish, jbn_add_item_str(n, 0, lname, -1, 0, pool));
    if (email) {
      utf8lwr(email);
      if (strcmp(lname, email) != 0) {
        RCC(rc, finish, jbn_add_item_str(n, 0, email, -1, 0, pool));
      }
    }
  }

  jql_destroy(&q);
  RCC(rc, finish, jql_create(&q, "users", "/[name = :?] | upsert :?"));
  RCC(rc, finish, jql_set_str3(q, 0, 0, vname.buf, vname.len));
  RCC(rc, finish, jql_set_json(q, 0, 1, user));
  RCC(rc, finish, ejdb_update(g_env.db, q));

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  } else if (error) {
    iwn_http_response_header_add(req->http, "X-Greenrooms-Error", error, -1);
    ret = 200;
  } else if (user) {
    jbn_detach(user, "/pwh");
    jbn_detach(user, "/salt");
    ret = iwn_http_response_jbn(req->http, user, 200, false);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
  return ret;
}

static int _remove(struct iwn_wf_req *req, void *d) {
  iwrc rc = 0;
  int ret = 500;
  JQL q = 0;
  uint64_t ctime;
  JBL_NODE update;
  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);
  struct iwn_val vid = iwn_pair_find_val(&req->form_params, "uuid", IW_LLEN("uuid"));
  const char *id = vid.len ? iwpool_strndup2(pool, vid.buf, vid.len) : 0;
  if (!iwu_uuid_valid(id)) {
    ret = 400;
    goto finish;
  }
  RCC(rc, finish, iwp_current_time_ms(&ctime, false));
  RCC(rc, finish, jbn_from_json("{}", &update, pool));
  RCC(rc, finish, jbn_add_item_i64(update, "ctime", ctime, 0, pool));
  RCC(rc, finish, jbn_add_item_bool(update, "removed", true, 0, pool));

  RCC(rc, finish, jql_create(&q, "users", "/[uuid = :?] | apply :?"));
  RCC(rc, finish, jql_set_str(q, 0, 0, id));
  RCC(rc, finish, jql_set_json(q, 0, 1, update));
  RCC(rc, finish, ejdb_update(g_env.db, q));

  ret = 200;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  iwpool_destroy(pool);
  return ret;
}

iwrc grh_route_adm_users(const struct iwn_wf_route *parent) {
  iwrc rc = 0;

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/update",
    .handler = _update,
    .flags = IWN_WF_POST
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/list",
    .handler = _list,
    .flags = IWN_WF_GET
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/remove",
    .handler = _remove,
    .flags = IWN_WF_POST
  }, 0));

finish:
  return rc;
}
