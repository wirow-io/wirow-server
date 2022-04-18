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

#include "rct_transport.h"

iwrc rct_transport_direct_create(
  wrc_resource_t  router_id,
  uint32_t        max_message_size,
  wrc_resource_t *transport_id_out
  ) {
  *transport_id_out = 0;

  iwrc rc = 0;
  wrc_msg_t *m = 0;
  bool locked = false;
  rct_router_t *router = 0;
  rct_transport_direct_t *transport;

  if (!max_message_size) {
    max_message_size = 262144;
  }

  IWPOOL *pool = iwpool_create(sizeof(*transport));
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  RCB(finish, transport = iwpool_calloc(sizeof(*transport), pool));
  transport->type = RCT_TYPE_TRANSPORT_DIRECT;
  transport->pool = pool;
  transport->max_message_size = max_message_size;
  iwu_uuid4_fill(transport->uuid);

  router = rct_resource_by_id_locked(router_id, RCT_TYPE_ROUTER, __func__), locked = true;
  RCIF(!router, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  rct_resource_ref_lk(transport, RCT_INIT_REFS, __func__);
  transport->router = rct_resource_ref_lk(router, 1, __func__);

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_ROUTER_CREATE_DIRECT_TRANSPORT
      }
    }
  }));
  RCC(rc, finish, jbl_clone(router->identity, &m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "transportId", transport->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &transport->identity, pool));

  RCC(rc, finish, jbl_create_empty_object(&m->input.worker.data));
  RCC(rc, finish, jbl_set_bool(m->input.worker.data, "direct", true));
  RCC(rc, finish, jbl_set_int64(m->input.worker.data, "maxMessageSize", max_message_size));

  rct_transport_t *pp = router->transports;
  router->transports = (void*) transport;
  transport->next = pp;
  transport->id = rct_resource_id_next_lk();

  RCC(rc, finish, rct_resource_register_lk(transport));
  rct_resource_unlock_keep_ref((void*) router), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  if (m->output.worker.data) {
    RCC(rc, finish, jbl_to_node(m->output.worker.data, &transport->data, true, pool));
    RCC(rc, finish, rct_transport_complete_registration(m->output.worker.data, (void*) transport));
  } else {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  wrc_notify_event_handlers(WRC_EVT_TRANSPORT_CREATED, transport->id, 0);
  *transport_id_out = transport->id;

finish:
  rct_resource_ref_keep_locking(transport, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(router, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}

iwrc rct_transport_direct_send_rtcp(wrc_resource_t transport_id, char *payload, size_t payload_len) {
  iwrc rc = 0;
  wrc_msg_t *m = 0;
  rct_transport_t *transport = 0;
  bool locked = false;

  transport = rct_resource_by_id_locked(transport_id, RCT_TYPE_TRANSPORT_ALL, __func__), locked = true;
  RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  if (transport->type != RCT_TYPE_TRANSPORT_DIRECT) {
    rc = RCT_ERROR_REQUIRED_DIRECT_TRANSPORT;
    goto finish;
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_PAYLOAD,
    .resource_id = transport->router->worker_id,
    .input = {
      .payload       = {
        .type        = WRC_PAYLOAD_RTCP_PACKET_SEND,
        .payload     = payload,
        .payload_len = payload_len
      }
    }
  }));
  RCC(rc, finish, jbl_create_empty_object(&m->input.payload.data));
  jbl_clone(transport->identity, &m->input.payload.internal);

  rct_resource_unlock_keep_ref(transport), locked = false;

  rc = wrc_send_and_wait(m, 0);

finish:
  rct_resource_ref_unlock(transport, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}
