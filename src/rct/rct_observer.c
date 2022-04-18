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

#include "rct_observer.h"
#include <assert.h>

void rct_observer_dispose_lk(rct_rtp_observer_t *o) {
  assert(o);
  if (o->router) {
    for (rct_rtp_observer_t *p = o->router->observers, *pp = 0; p; p = p->next) {
      if (p->id == o->id) {
        if (pp) {
          pp->next = p->next;
        } else {
          o->router->observers = p->next;
        }
        break;
      } else {
        pp = p;
      }
    }
  }
  rct_resource_ref_lk(o->router, -1, __func__); // Unref parent router
}

void rct_observer_close_lk(rct_rtp_observer_t *o) {
}

static iwrc _observer_cmd_producer(wrc_resource_t observer_id, wrc_resource_t producer_id, wrc_worker_cmd_e cmd) {
  JBL jbl;
  rct_resource_base_t b;

  iwrc rc = RCR(rct_resource_probe_by_id(producer_id, &b));

  RCR(jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "producerId", b.uuid));

  rc = rct_resource_json_command2(&(struct rct_json_command_spec) {
    .resource_id = observer_id,
    .cmd = cmd,
    .resource_type = RCT_TYPE_OBSERVER_ALL,
    .cmd_data = jbl
  });

finish:
  jbl_destroy(&jbl);
  return rc;
}

iwrc rct_observer_close(wrc_resource_t observer_id) {
  iwrc rc = rct_resource_json_command(observer_id, WRC_CMD_RTP_OBSERVER_CLOSE, 0, 0, 0);
  if (!rc) {
    wrc_notify_event_handlers(WRC_EVT_OBSERVER_CLOSED, observer_id, 0);
  }
  return rc;
}

iwrc rct_observer_add_producer(wrc_resource_t observer_id, wrc_resource_t producer_id) {
  return _observer_cmd_producer(observer_id, producer_id, WRC_CMD_RTP_OBSERVER_ADD_PRODUCER);
}

iwrc rct_observer_remove_producer(wrc_resource_t observer_id, wrc_resource_t producer_id) {
  return _observer_cmd_producer(observer_id, producer_id, WRC_CMD_RTP_OBSERVER_REMOVE_PRODUCER);
}

static iwrc _observer_pause_resume(wrc_resource_t observer_id, bool pause_resume) {
  iwrc rc = 0;
  bool paused = false, locked = false;
  rct_rtp_observer_t *o = rct_resource_by_id_locked(observer_id, RCT_TYPE_OBSERVER_ALL, __func__);
  locked = true;
  RCIF(!o, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);
  paused = o->paused;
  rct_unlock(), locked = false;
  rc = rct_resource_json_command(observer_id,
                                 pause_resume ? WRC_CMD_RTP_OBSERVER_PAUSE : WRC_CMD_RTP_OBSERVER_RESUME,
                                 RCT_TYPE_OBSERVER_ALL, 0, 0);
  if (!rc) {
    rct_lock(), locked = true;
    o->paused = pause_resume;
    if (!paused && pause_resume) {
      wrc_notify_event_handlers(WRC_EVT_OBSERVER_PAUSED, observer_id, 0);
    }
    if (paused && !pause_resume) {
      wrc_notify_event_handlers(WRC_EVT_OBSERVER_RESUMED, observer_id, 0);
    }
  }

finish:
  rct_resource_ref_unlock(o, locked, -1, __func__);
  return rc;
}

iwrc rct_observer_pause(wrc_resource_t observer_id) {
  return _observer_pause_resume(observer_id, true);
}

iwrc rct_observer_resume(wrc_resource_t observer_id) {
  return _observer_pause_resume(observer_id, false);
}
