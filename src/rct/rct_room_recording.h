#pragma once

#include "grh.h"
#include "rct.h"

iwrc rct_room_recording_start(wrc_resource_t room_id);

iwrc rct_room_recording_stop(wrc_resource_t room_id);

iwrc rct_room_recording_module_init(void);

void rct_room_recording_module_close(void);
