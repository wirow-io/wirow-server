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

#include "rct_consumer.h"

iwrc rct_consumer_data_create(
  wrc_resource_t  transport_id,
  wrc_resource_t  producer_id,
  int             ordered,
  int             max_packet_lifetime_ms,
  int             max_retransmits_ms,
  wrc_resource_t *consumer_out
  ) {
  *consumer_out = 0;

  iwrc rc = 0;
  const char *type;
  JBL_NODE sctp_stream_parameters = 0,
           data, n1;
  IWPOOL *pool, *pool_tmp = 0;
  wrc_msg_t *m = 0;
  bool locked = false;
  rct_producer_data_t *producer = 0;
  rct_consumer_data_t *consumer = 0;
  rct_transport_t *transport = 0;

  pool = iwpool_create(sizeof(*consumer));
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  consumer = iwpool_calloc(sizeof(*consumer), pool);
  consumer->pool = pool;
  consumer->type = RCT_TYPE_CONSUMER_DATA;
  iwu_uuid4_fill(consumer->uuid);

  RCB(finish, pool_tmp = iwpool_create_empty());

  producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER_DATA, __func__), locked = true;
  RCIF(!producer, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  transport = rct_resource_ref_lk(rct_resource_by_id_unsafe(transport_id, RCT_TYPE_TRANSPORT_ALL), 1, __func__);
  RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  if (transport->type != RCT_TYPE_TRANSPORT_DIRECT) {
    type = "sctp";
    consumer->data_type = RCT_DATA_SCTP;

    if (!producer->sctp_stream_parameters) {
      iwlog_error("RCT Producer does't have sctp_stream_parameters");
      rc = RCT_ERROR_INVALID_RESOURCE_CONFIGURATION;
      goto finish;
    }

    RCC(rc, finish, jbn_clone(producer->sctp_stream_parameters,
                              &sctp_stream_parameters, pool_tmp));

    if (ordered) {
      RCC(rc, finish, jbn_copy_path(&(struct _JBL_NODE) {
        .type = JBV_BOOL,
        .vbool = ordered > 0
      }, "/", sctp_stream_parameters, "/ordered", false, false, pool_tmp));
    }

    if (max_packet_lifetime_ms > 0) {
      RCC(rc, finish, jbn_copy_path(&(struct _JBL_NODE) {
        .type = JBV_I64,
        .vi64 = max_packet_lifetime_ms
      }, "/", sctp_stream_parameters, "/maxPacketLifeTime", false, false, pool_tmp));
    }

    if (max_retransmits_ms > 0) {
      RCC(rc, finish, jbn_copy_path(&(struct _JBL_NODE) {
        .type = JBV_I64,
        .vi64 = max_retransmits_ms
      }, "/", sctp_stream_parameters, "/maxRetransmits", false, false, pool_tmp));
    }

    int streamId = rct_transport_next_id_lk(transport);
    RCC(rc, finish, jbn_copy_path(&(struct _JBL_NODE) {
      .type = JBV_I64,
      .vi64 = streamId
    }, "/", sctp_stream_parameters, "/streamId", false, false, pool_tmp));
  } else {
    type = "direct";
    consumer->data_type = RCT_DATA_DIRECT;
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = transport->router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_TRANSPORT_CONSUME_DATA
      }
    }
  }));

  // Build identity
  RCC(rc, finish, jbl_clone(transport->identity, &m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "dataConsumerId", consumer->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &consumer->identity, pool));

  // Build command data
  RCC(rc, finish, jbn_from_json("{}", &data, pool_tmp));
  RCC(rc, finish, jbn_add_item_str(data, "dataProducerId", producer->uuid, -1, 0, pool_tmp));
  RCC(rc, finish, jbn_add_item_str(data, "type", type, -1, 0, pool_tmp));
  if (sctp_stream_parameters) {
    sctp_stream_parameters->key = "sctpStreamParameters";
    sctp_stream_parameters->klidx = sizeof("sctpStreamParameters") - 1;
    jbn_add_item(data, sctp_stream_parameters);
  }
  if (producer->label) {
    RCC(rc, finish, jbn_add_item_str(data, "label", producer->label, -1, 0, pool_tmp));
  }
  if (producer->protocol) {
    RCC(rc, finish, jbn_add_item_str(data, "protocol", producer->protocol, -1, 0, pool_tmp));
  }
  RCC(rc, finish, jbl_from_node(&m->input.worker.data, data));

  consumer->producer = rct_resource_ref_lk(producer, 1, __func__);

  rct_consumer_base_t *pp = producer->consumers;
  producer->consumers = rct_resource_ref_lk(consumer, RCT_INIT_REFS, __func__);
  consumer->next = pp;

  consumer->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(consumer));
  rct_resource_unlock_keep_ref(producer), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  if (m->output.worker.data) {
    RCC(rc, finish, jbl_to_node(m->output.worker.data, &data, true, pool));
    jbn_at(data, "/data/sctpStreamParameters", &consumer->sctp_stream_parameters);
    RCC(rc, finish, jbn_at(data, "/data/protocol", &n1));
    if (n1->type == JBV_STR) {
      consumer->protocol = n1->vptr;
    } else {
      consumer->protocol = "";
    }
    RCC(rc, finish, jbn_at(data, "/data/label", &n1));
    if (n1->type == JBV_STR) {
      consumer->label = n1->vptr;
    } else {
      consumer->label = "";
    }
  } else {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  wrc_notify_event_handlers(WRC_EVT_CONSUMER_CREATED, consumer->id, 0);
  *consumer_out = consumer->id;

finish:
  rct_resource_ref_keep_locking(consumer, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_keep_locking(transport, locked, -1, __func__);
  rct_resource_ref_unlock(producer, locked, -1, __func__);
  wrc_msg_destroy(m);
  iwpool_destroy(pool_tmp);
  return rc;
}
