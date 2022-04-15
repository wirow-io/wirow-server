#pragma once

#include "grh.h"

#define  GAUGE_ROOMS        0x01U   ///< Number of rooms
#define  GAUGE_ROOM_USERS   0x02U   ///< Number of room users
#define  GAUGE_WORKERS      0x04U   ///< Number of mediasoup workers
#define  GAUGE_STREAMS      0x08U   ///< Number of network WebRTC streams

iwrc gr_gauge_set_async(uint32_t gauge, int64_t level);

iwrc gr_gauges_maintain(void);

iwrc grh_route_gauges(const struct iwn_wf_route *parent);

