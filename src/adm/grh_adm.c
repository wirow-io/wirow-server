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

#include "grh_adm.h"
#include "grh_adm_users.h"
#include "gr_gauges.h"

iwrc grh_route_adm(const struct iwn_wf_route *parent) {
  iwrc rc;
  struct iwn_wf_route *user, *gauges;

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/user",
    .flags = IWN_WF_METHODS_ALL,
  }, &user));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/gauges"
  }, &gauges));

  RCC(rc, finish, grh_route_adm_users(user));
  RCC(rc, finish, grh_route_gauges(gauges));

finish:
  return rc;
}
