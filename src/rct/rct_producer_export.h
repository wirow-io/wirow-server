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

#include "rct_producer.h"

//
// Producer stream export
//

iwrc rct_producer_export(
  wrc_resource_t                      producer_id,
  const rct_producer_export_params_t *params,
  wrc_resource_t                     *out_export_id);

void rct_producer_export_close(wrc_resource_t export_id);

iwrc rct_export_consumer_pause(wrc_resource_t export_id);

iwrc rct_export_consumer_resume(wrc_resource_t export_id);
