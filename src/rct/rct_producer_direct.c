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

#include "rct_producer.h"

iwrc rct_producer_direct_send_rtp_packet(wrc_resource_t producer_id, char *payload, size_t payload_len) {
  iwrc rc = 0;
  wrc_msg_t *m = 0;
  rct_producer_t *producer = 0;
  bool locked = false;

  producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER, __func__), locked = true;
  RCIF(!producer, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  if (producer->transport->type != RCT_TYPE_TRANSPORT_DIRECT) {
    rc = RCT_ERROR_REQUIRED_DIRECT_TRANSPORT;
    goto finish;
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_PAYLOAD,
    .resource_id = producer->transport->router->worker_id,
    .input = {
      .payload       = {
        .type        = WRC_PAYLOAD_RTP_PACKET_SEND,
        .payload     = payload,
        .payload_len = payload_len
      }
    }
  }));

  RCC(rc, finish, jbl_create_empty_object(&m->input.payload.data));

  jbl_clone(producer->identity, &m->input.payload.internal);
  rct_resource_unlock_keep_ref(producer), locked = false;
  rc = wrc_send_and_wait(m, 0);

finish:
  rct_resource_ref_unlock(producer, locked, -1, __func__);
  wrc_msg_destroy(m);
  return rc;
}
