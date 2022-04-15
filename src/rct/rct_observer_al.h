#pragma once

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
