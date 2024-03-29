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
#include "rct_router.h"

#include <iowow/iwarr.h>

static void _rct_on_worker_shutdown(wrc_resource_t worker_id) {
  rct_lock();
  IWHMAP_ITER iter;
  iwhmap_iter_init(state.map_id2ptr, &iter);
  while (iwhmap_iter_next(&iter)) {
    const rct_resource_base_t *h = iter.val;
    if (h->type == RCT_TYPE_ROUTER) {
      rct_router_t *router = (void*) h;
      if (worker_id == -1 || router->worker_id == worker_id) {
        wrc_notify_event_handlers(WRC_EVT_ROUTER_CLOSED, router->id, 0);
      }
    }
  }
  rct_unlock();
}

iwrc rct_worker_acquire_for_router(wrc_resource_t *worker_id_out) {
  return wrc_worker_acquire(worker_id_out);
}

iwrc rct_worker_dump(wrc_resource_t worker_id, JBL *dump_out) {
  *dump_out = 0;
  iwrc rc = 0;
  wrc_msg_t *m;

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .worker_id = worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_WORKER_DUMP
      }
    }
  }));

  rc = wrc_send_and_wait(m, 0);
  if (!rc) {
    *dump_out = m->output.worker.data;
    m->output.worker.data = 0;
  }

finish:
  if (m) {
    wrc_msg_destroy(m);
  }
  return rc;
}

iwrc rct_worker_shutdown(wrc_resource_t worker_id) {
  wrc_worker_kill(worker_id);
  return 0;
}

static iwrc _rct_event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  switch (evt) {
    case WRC_EVT_WORKER_SHUTDOWN:
      _rct_on_worker_shutdown(resource_id);
      break;
    default:
      break;
  }
  return 0;
}

static uint32_t _event_handler_id;

iwrc rct_worker_module_init(void) {
  return wrc_add_event_handler(_rct_event_handler, 0, &_event_handler_id);
}

void rct_worker_module_shutdown(void) {
}

void rct_worker_module_destroy(void) {
  wrc_remove_event_handler(_event_handler_id);
}
