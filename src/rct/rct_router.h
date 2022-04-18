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

#include "rct_worker.h"

/**
 * @brief Create a new router.
 *
 * @param uuid Optional router uuid.
 * @param worker_id Optional worker id.
 * @param[out] router_id_out Router handle plecaholder.
 */
iwrc rct_router_create(
  const char     *uuid,
  wrc_resource_t  worker_id,
  wrc_resource_t *router_id_out);

iwrc rct_router_dump(wrc_resource_t router_id, JBL *dump_out);

iwrc rct_router_close(wrc_resource_t router_id);
