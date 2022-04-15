#pragma once
#include "grh.h"

#define GR_USER_SESSION_KEY        "__user__"
#define GR_USER_ID_SESSION_KEY     "__userid__"
#define GR_PERMISSIONS_SESSION_KEY "__perms__"

iwrc grh_auth_routes(const struct iwn_wf_route *parent, struct iwn_wf_route **out);

void grh_auth_request_init(struct iwn_wf_req *req);

/**
 * @brief Returns username of logged user or zero.
 */
const char* grh_auth_username(struct iwn_wf_req *req);

/**
 * @brief Returns true if user session has any of permissions specified as comma separated string.
 */
bool grh_auth_has_any_perms(struct iwn_wf_req *req, const char *perms);


/**
 * @brief Returns id of logged user otherwise zero.
 */
int64_t grh_auth_get_userid(struct iwn_wf_req *req);

/**
 * @brief Returns true if user identified by `user_id` participated in the room.
 */
bool grh_auth_is_room_member(const char *room_cid, int64_t user_id, bool *out_room_owner);
