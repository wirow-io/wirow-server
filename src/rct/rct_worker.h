#pragma once

#include "rct.h"

iwrc rct_worker_acquire_for_router(wrc_resource_t *worker_id_out);

iwrc rct_worker_dump(wrc_resource_t worker_id, JBL *dump_out);

iwrc rct_worker_shutdown(wrc_resource_t worker_id);
