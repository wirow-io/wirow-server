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

#define  GAUGE_ROOMS        0x01U   ///< Number of rooms
#define  GAUGE_ROOM_USERS   0x02U   ///< Number of room users
#define  GAUGE_WORKERS      0x04U   ///< Number of mediasoup workers
#define  GAUGE_STREAMS      0x08U   ///< Number of network WebRTC streams

iwrc gr_gauge_set_async(uint32_t gauge, int64_t level);

iwrc gr_gauges_maintain(void);

iwrc grh_route_gauges(const struct iwn_wf_route *parent);

