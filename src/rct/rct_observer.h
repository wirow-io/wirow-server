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

#include "rct.h"

iwrc rct_observer_close(wrc_resource_t observer_id);

iwrc rct_observer_pause(wrc_resource_t observer_id);

iwrc rct_observer_resume(wrc_resource_t observer_id);

void rct_observer_close_lk(rct_rtp_observer_t *o);

void rct_observer_dispose_lk(rct_rtp_observer_t *o);

iwrc rct_observer_add_producer(wrc_resource_t observer_id, wrc_resource_t producer_id);

iwrc rct_observer_remove_producer(wrc_resource_t observer_id, wrc_resource_t producer_id);
