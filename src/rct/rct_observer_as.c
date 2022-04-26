#include "rct_observer_as.h"

iwrc rct_observer_as_create(
  wrc_resource_t  router_id,
  int             interval_ms,
  wrc_resource_t *out_observer_id
  ) {
  if (!out_observer_id) {
    return IW_ERROR_INVALID_ARGS;
  }
  *out_observer_id = 0;
  if (!interval_ms) {
    interval_ms = 300;
  }
  iwrc rc = 0;
  bool locked = false;
  rct_rtp_observer_t *obs = 0;
  rct_router_t *router = 0;
  wrc_msg_t *m = 0;

  IWPOOL *pool = iwpool_create_empty();
  RCB(finish, pool);

  RCB(finish, obs = iwpool_calloc(sizeof(*obs), pool));
  obs->type = RCT_TYPE_OBSERVER_AS;
  obs->pool = pool;
  obs->close = (void*) rct_observer_close_lk;
  obs->dispose = (void*) rct_observer_dispose_lk;
  iwu_uuid4_fill(obs->uuid);

  router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__), locked = true;
  RCIF(!router, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  rct_resource_ref_lk(obs, RCT_INIT_REFS, __func__);
  obs->router = rct_resource_ref_lk(router, 1, __func__);

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_ROUTER_CREATE_ACTIVE_SPEAKER_OBSERVER
      }
    }
  }));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  RCC(rc, finish, jbl_set_int64(m->input.worker.data, "interval", interval_ms));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "routerId", router->uuid));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "rtpObserverId", obs->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &obs->identity, pool));

  rct_rtp_observer_t *o = router->observers;
  router->observers = obs;
  obs->next = o;

  obs->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(obs));
  rct_resource_unlock_keep_ref(router), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  wrc_notify_event_handlers(WRC_EVT_OBSERVER_CREATED, obs->id, 0);
  *out_observer_id = obs->id;

finish:
  rct_resource_ref_keep_locking(obs, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(router, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}
