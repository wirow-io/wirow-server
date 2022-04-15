#include "grh_recording.h"
#include "grh_auth.h"
#include "utils/files.h"

#include <ejdb2/ejdb2.h>
#include <iwnet/iwn_mimetypes.h>
#include <iwnet/iwn_wf_files.h>

#include <string.h>
#include <stdlib.h>

static int _handler_get(struct iwn_wf_req *req, void *d) {
  const char *cid = strrchr(req->path_matched, '/');
  if (!cid) {
    return 400;
  }
  ++cid;

  iwrc rc = 0;
  int ret = 500;
  JQL q = 0;
  JBL_NODE n, room;
  IWXSTR *xstr = 0;
  EJDB_LIST list = 0;
  bool owner = false;
  char cid_dir[FILES_UUID_DIR_BUFSZ];

  int64_t user_id = grh_auth_get_userid(req);
  if (!user_id) {
    return 403;
  }
  RCC(rc, finish, jql_create(&q, "rooms", "/[uuid = :?] or /[cid = :?] | /events + /recf"));
  RCC(rc, finish, jql_set_str(q, 0, 0, cid));
  RCC(rc, finish, jql_set_str(q, 0, 1, cid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));

  if (!list->first || !grh_auth_is_room_member(cid, user_id, &owner)) {
    ret = 403;
    goto finish;
  }

  room = list->first->node;
  rc = jbn_at(room, "/recf", &n);
  if (rc || n->type != JBV_STR) {
    rc = 0;
    ret = 404;
    goto finish;
  }

  files_uuid_dir_copy(cid, cid_dir);

  RCB(finish, xstr = iwxstr_new());
  RCC(rc, finish, iwxstr_cat2(xstr, g_env.recording.dir));
  RCC(rc, finish, iwxstr_cat(xstr, "/", 1));
  RCC(rc, finish, iwxstr_cat2(xstr, cid_dir));
  RCC(rc, finish, iwxstr_cat(xstr, "/", 1));
  RCC(rc, finish, iwxstr_cat2(xstr, n->vptr));

  const char *path = iwxstr_ptr(xstr);
  const char *ctype = iwn_mimetype_find_by_path(path);
  if (ctype == 0) {
    ctype = "video/webm";
  }

  ret = iwn_wf_file_serve(req, ctype, path);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  iwxstr_destroy(xstr);
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  return ret;
}

iwrc grh_route_recording(struct iwn_wf_route *parent) {
  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/rec",
    .flags = IWN_WF_GET | IWN_WF_HEAD
  }, &parent));

  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "^/[a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12}",
    .handler = _handler_get,
    .flags = IWN_WF_GET | IWN_WF_HEAD
  }, 0);
}
