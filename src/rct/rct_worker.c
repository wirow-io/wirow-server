#include "rct_worker.h"
#include "rct_router.h"

#include <ejdb2/iowow/iwarr.h>

static void _rct_on_worker_shutdown(wrc_resource_t worker_id) {
  bool locked = true;
  rct_lock();

  IWSTREE_ITER iter;
  IWSTREE *tree = rct_state.map_id2ptr;
  iwrc rc = iwstree_iter_init(tree, &iter);
  RCGO(rc, finish);

  while (iwstree_iter_has_next(&iter)) {
    rct_resource_base_t *h;
    RCC(rc, finish, iwstree_iter_next(&iter, 0, (void**) &h));
    if (h->type == RCT_TYPE_ROUTER) {
      rct_router_t *router = (void*) h;
      if (worker_id == -1 || router->worker_id == worker_id) {
        wrc_notify_event_handlers(WRC_EVT_ROUTER_CLOSED, router->id, 0);
      }
    }
  }

finish:
  iwstree_iter_close(&iter);
  if (locked) {
    rct_unlock();
  }
  if (rc) {
    iwlog_ecode_error3(rc);
  }
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
    .resource_id = worker_id,
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

void rct_worker_module_close(void) {
  wrc_remove_event_handler(_event_handler_id);
}
