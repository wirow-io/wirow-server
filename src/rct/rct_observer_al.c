#include "rct_observer_al.h"

iwrc rct_observer_al_create(
  wrc_resource_t  router_id,
  int             max_entries,
  int             threshold,
  int             interval_ms,
  wrc_resource_t *out_observer_id
  ) {
  if (!out_observer_id) {
    return IW_ERROR_INVALID_ARGS;
  }
  if (!max_entries) {
    max_entries = 1;
  }
  if (!threshold) {
    threshold = -80;
  }
  if (!interval_ms) {
    interval_ms = 1000;
  }

  *out_observer_id = 0;

  iwrc rc = 0;
  bool locked = false;
  rct_rtp_observer_t *alo = 0;
  rct_router_t *router = 0;
  wrc_msg_t *m = 0;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCB(finish, alo = iwpool_calloc(sizeof(*alo), pool));
  alo->type = RCT_TYPE_OBSERVER_AL;
  alo->pool = pool;
  alo->close = (void*) rct_observer_close_lk;
  alo->dispose = (void*) rct_observer_dispose_lk;
  iwu_uuid4_fill(alo->uuid);

  router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__), locked = true;
  RCIF(!router, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  rct_resource_ref_lk(alo, RCT_INIT_REFS, __func__);
  alo->router = rct_resource_ref_lk(router, 1, __func__);

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_ROUTER_CREATE_AUDIO_LEVEL_OBSERVER
      }
    }
  }));
  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  RCC(rc, finish, jbl_set_int64(m->input.worker.data, "maxEntries", max_entries));
  RCC(rc, finish, jbl_set_int64(m->input.worker.data, "threshold", threshold));
  RCC(rc, finish, jbl_set_int64(m->input.worker.data, "interval", interval_ms));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "routerId", router->uuid));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "rtpObserverId", alo->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &alo->identity, pool));

  rct_rtp_observer_t *o = router->observers;
  router->observers = alo;
  alo->next = o;

  alo->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(alo));
  rct_resource_unlock_keep_ref(router), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  wrc_notify_event_handlers(WRC_EVT_OBSERVER_CREATED, alo->id, 0);
  *out_observer_id = alo->id;

finish:
  rct_resource_ref_keep_locking(alo, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(router, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}
