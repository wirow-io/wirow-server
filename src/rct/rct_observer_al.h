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

/**
 * Audio level observer.
 */

#include "rct_observer.h"

/**
 * @brief Create audio level observer.
 *
 * @param router_id   Router reference.
 * @param max_entries Maximum number of entries in the `volumes` event. Default 16.
 * @param threshold   Minimum average volume (in dBvo from -127 to 0) for entries in the
 *                    `volumes` event.  Default -60.
 * @param interval_ms Interval in ms for checking audio volumes. Default 800.
 * @param out_alo_id  Created obsherver instance handler.
 * @return iwrc
 */
iwrc rct_observer_al_create(
  wrc_resource_t  router_id,
  int             max_entries,
  int             threshold,
  int             interval_ms,
  wrc_resource_t *out_observer_id);
