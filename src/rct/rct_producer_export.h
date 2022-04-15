#pragma once

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
