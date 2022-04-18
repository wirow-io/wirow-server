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

iwrc rct_worker_module_init(void);
iwrc rct_producer_export_module_init(void);
iwrc rct_consumer_module_init(void);
iwrc rct_room_module_init(void);

void rct_worker_module_shutdown(void);
void rct_worker_module_close(void);
void rct_producer_export_module_close(void);
void rct_consumer_module_close(void);
void rct_room_module_close(void);

void rct_router_close_lk(rct_router_t*);
void rct_transport_close_lk(rct_transport_t*);
void rct_transport_dispose_lk(rct_transport_t*);
void rct_producer_close_lk(rct_producer_base_t*);
void rct_producer_dispose_lk(rct_producer_base_t*);
void rct_consumer_dispose_lk(rct_consumer_base_t*);

