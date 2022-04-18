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
