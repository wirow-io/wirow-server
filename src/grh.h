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

#include "gr.h"
#include "iwnet_extra.h"

extern struct gr_env g_env;

#define Q(s)    #s
#define P(s, v) "\""Q (s) "\":"#v
#define N(s, v) P(s, v) ","

#define REQ_FLAG_ROLE_USER  0x01U
#define REQ_FLAG_ROLE_ADMIN 0x02U
#define REQ_FLAG_AUTH_OK    0x04U
#define REQ_FLAG_ROLE_ALL   (REQ_FLAG_ROLE_USER | REQ_FLAG_ROLE_ADMIN)
