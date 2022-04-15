#pragma once

#include "rct.h"

iwrc rct_observer_close(wrc_resource_t observer_id);

iwrc rct_observer_pause(wrc_resource_t observer_id);

iwrc rct_observer_resume(wrc_resource_t observer_id);

void rct_observer_close_lk(rct_rtp_observer_t *o);

void rct_observer_dispose_lk(rct_rtp_observer_t *o);

iwrc rct_observer_add_producer(wrc_resource_t observer_id, wrc_resource_t producer_id);

iwrc rct_observer_remove_producer(wrc_resource_t observer_id, wrc_resource_t producer_id);
