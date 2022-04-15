#pragma once

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
