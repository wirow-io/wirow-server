#include "rct_producer.h"
#include <assert.h>

#define REPORT(msg_)                         \
  iwlog_error2(msg_);                        \
  return RCT_ERROR_INVALID_SCTP_STREAM_PARAMETERS;

static iwrc _rct_validate_sctp_stream_parameters(JBL_NODE n, IWPOOL *pool) {
  iwrc rc = 0, rc2;
  JBL_NODE n1, n2;
  bool ordered_given = false,
       ordered;

  if (n->type != JBV_OBJECT) {
    REPORT("params is not an object");
  }
  rc2 = jbn_at(n, "/streamId", &n1);
  if (rc2 || (n1->type != JBV_I64)) {
    REPORT("missing params.streamId");
  }
  rc2 = jbn_at(n, "/ordered", &n1);
  if (!rc2 && (n1->type == JBV_BOOL)) {
    ordered_given = true;
    ordered = n1->vbool;
  } else {
    ordered = true;
    if (!rc2) {
      n1->type = JBV_BOOL;
      n1->child = 0;
      n1->vbool = true;
    } else {
      RCC(rc, finish, jbn_add_item_bool(n, "ordered", true, 0, pool));
    }
  }

  // maxPacketLifeTime is optional.
  rc2 = jbn_at(n, "/maxPacketLifeTime", &n1);
  if (!rc2 && (n1->type != JBV_I64)) {
    REPORT("invalid params.maxPacketLifeTime");
  }

  // maxRetransmits is optional.
  rc2 = jbn_at(n, "/maxRetransmits", &n2);
  if (!rc2 && (n2->type != JBV_I64)) {
    REPORT("invalid params.maxRetransmits");
  }

  if ((n1->type == n2->type) && (n1->type == JBV_I64)) {
    REPORT("cannot provide both maxPacketLifeTime and maxRetransmits");
  }

  if (ordered_given && ordered && ((n1->type == JBV_I64) || (n2->type == JBV_I64))) {
    REPORT("cannot be ordered with maxPacketLifeTime or maxRetransmits");
  } else if (!ordered_given && ((n1->type == JBV_I64) || (n2->type == JBV_I64))) {
    rc2 = jbn_at(n, "/ordered", &n1);
    if (rc2) {
      RCC(rc, finish, jbn_add_item_bool(n, "ordered", false, 0, pool));
    } else {
      n1->type = JBV_BOOL;
      n1->child = 0;
      n1->vbool = false;
    }
  }

finish:
  return rc;
}

iwrc rct_producer_data_spec_create(
  const char                *sctp_stream_parameters,
  const char                *label,
  const char                *protocol,
  rct_producer_data_spec_t **spec_out
  ) {
  *spec_out = 0;

  iwrc rc = 0;
  IWPOOL *pool = iwpool_create(255);
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  rct_producer_data_spec_t *spec = iwpool_calloc(sizeof(*spec), pool);
  RCA(spec, finish);
  spec->pool = pool;

  if (sctp_stream_parameters) {
    RCC(rc, finish, jbn_from_json(sctp_stream_parameters, &spec->sctp_stream_parameters, pool));
  }

  if (label) {
    spec->label = iwpool_strdup(pool, label, &rc);
    RCGO(rc, finish);
  }

  if (protocol) {
    spec->protocol = iwpool_strdup(pool, protocol, &rc);
    RCGO(rc, finish);
  }

finish:
  if (rc) {
    iwpool_destroy(pool);
  } else {
    *spec_out = spec;
  }
  return rc;
}

iwrc rct_producer_data_create(
  wrc_resource_t                  transport_id,
  const rct_producer_data_spec_t *spec,
  wrc_resource_t                 *producer_out
  ) {
  if (  !spec
     || !producer_out
     || (spec->uuid && (strlen(spec->uuid) != IW_UUID_STR_LEN))) {
    return IW_ERROR_INVALID_ARGS;
  }
  *producer_out = 0;

  iwrc rc = 0;
  JBL_NODE sctp_stream_parameters = 0, data, n1;
  rct_producer_data_t *producer = 0;
  rct_transport_t *transport = 0;
  wrc_msg_t *m = 0;
  bool locked = false;

  IWPOOL *pool = spec->pool, *pool_tmp = 0;
  RCB(finish, pool_tmp = iwpool_create_empty());

  transport = rct_resource_by_id_locked(transport_id, RCT_TYPE_TRANSPORT_ALL, __func__), locked = true;
  RCIF(!transport, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  RCB(finish, producer = iwpool_calloc(sizeof(*producer), pool));
  producer->pool = pool;
  producer->type = RCT_TYPE_PRODUCER_DATA;
  if (spec->uuid) {
    memcpy(producer->uuid, spec->uuid, sizeof(producer->uuid));
  } else {
    iwu_uuid4_fill(producer->uuid);
  }
  if (spec->label) {
    producer->label = spec->label;
  } else {
    producer->label = "";
  }
  if (spec->protocol) {
    producer->protocol = spec->protocol;
  } else {
    producer->protocol = "";
  }

  if (transport->type != RCT_TYPE_TRANSPORT_DIRECT) {
    producer->data_type = RCT_DATA_SCTP;
    sctp_stream_parameters = spec->sctp_stream_parameters;
  } else {
    producer->data_type = RCT_DATA_DIRECT;
    if (spec->sctp_stream_parameters) {
      iwlog_warn2("sctpStreamParameters are ignored when producing data on a DirectTransport");
    }
  }

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = transport->router->worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_TRANSPORT_PRODUCE_DATA
      }
    }
  }));

  // Build identity
  RCC(rc, finish, jbl_clone(transport->identity, &m->input.worker.internal));
  RCC(rc, finish, jbl_set_string(m->input.worker.internal, "dataProducerId", producer->uuid));
  RCC(rc, finish, jbl_clone_into_pool(m->input.worker.internal, &producer->identity, pool));

  // Build command data
  RCC(rc, finish, jbn_from_json("{}", &data, pool_tmp));
  if (producer->data_type == RCT_DATA_SCTP) {
    RCC(rc, finish, jbn_add_item_str(data, "type", "sctp", sizeof("sctp") - 1, 0, pool_tmp));
  } else if (producer->data_type == RCT_DATA_DIRECT) {
    RCC(rc, finish, jbn_add_item_str(data, "type", "direct", sizeof("direct") - 1, 0, pool_tmp));
  }
  if (sctp_stream_parameters) {
    sctp_stream_parameters->key = "sctpStreamParameters";
    sctp_stream_parameters->klidx = sizeof("sctpStreamParameters") - 1;
    jbn_add_item(data, sctp_stream_parameters);
  }
  RCC(rc, finish, jbn_add_item_str(data, "label", producer->label, -1, 0, pool_tmp));
  RCC(rc, finish, jbn_add_item_str(data, "protocol", producer->protocol, -1, 0, pool_tmp));
  RCC(rc, finish, jbl_from_node(&m->input.worker.data, data));

  rct_producer_base_t *pp = transport->producers;
  transport->producers = rct_resource_ref_lk(producer, RCT_INIT_REFS, __func__);
  producer->next = pp;
  producer->transport = rct_resource_ref_lk(transport, 1, __func__);

  producer->id = rct_resource_id_next_lk();
  RCC(rc, finish, rct_resource_register_lk(producer));
  rct_resource_unlock_keep_ref(transport), locked = false;

  // Send command to worker
  RCC(rc, finish, wrc_send_and_wait(m, 0));

  if (m->output.worker.data) {
    RCC(rc, finish, jbl_to_node(m->output.worker.data, &data, true, pool));
    jbn_at(data, "/data/sctpStreamParameters", &producer->sctp_stream_parameters);
    RCC(rc, finish, jbn_at(data, "/data/protocol", &n1));
    if (n1->type == JBV_STR) {
      producer->protocol = n1->vptr;
    } else {
      producer->protocol = "";
    }
    RCC(rc, finish, jbn_at(data, "/data/label", &n1));
    if (n1->type == JBV_STR) {
      producer->label = n1->vptr;
    } else {
      producer->label = "";
    }
  } else {
    rc = GR_ERROR_WORKER_UNEXPECTED_DATA_RECEIVED;
    goto finish;
  }

  wrc_notify_event_handlers(WRC_EVT_PRODUCER_CREATED, producer->id, 0);
  *producer_out = producer->id;

finish:
  rct_resource_ref_keep_locking(producer, locked, rc ? -RCT_INIT_REFS : -RCT_INIT_REFS + 1, __func__);
  rct_resource_ref_unlock(transport, locked, -1, __func__);
  wrc_msg_destroy(m);
  iwpool_destroy(pool_tmp);
  return rc;
}

iwrc rct_producer_data_close(wrc_resource_t producer_id) {
  iwrc rc = rct_resource_json_command(producer_id, WRC_CMD_DATA_PRODUCER_CLOSE, 0, 0, 0);
  if (!rc) {
    wrc_notify_event_handlers(WRC_EVT_PRODUCER_CLOSED, producer_id, 0);
  }
  return rc;
}

iwrc rct_producer_data_send(
  wrc_resource_t producer_id,
  char *payload, size_t payload_len,
  bool binary_message, int32_t ppid
  ) {
  iwrc rc = 0;
  wrc_msg_t *m = 0;
  rct_producer_data_t *producer = 0;
  bool locked = false;

  /*
   * +-------------------------------+----------+
   * | Value                         | SCTP     |
   * |                               | PPID     |
   * +-------------------------------+----------+
   * | WebRTC String                 | 51       |
   * | WebRTC Binary Partial         | 52       |
   * | (Deprecated)                  |          |
   * | WebRTC Binary                 | 53       |
   * | WebRTC String Partial         | 54       |
   * | (Deprecated)                  |          |
   * | WebRTC String Empty           | 56       |
   * | WebRTC Binary Empty           | 57       |
   * +-------------------------------+----------+
   */

  if (!ppid) {
    ppid = binary_message
           ? payload_len > 0 ? 53 : 57
           : payload_len > 0 ? 51 : 56;
  }

  producer = rct_resource_by_id_locked(producer_id, RCT_TYPE_PRODUCER_DATA, __func__), locked = true;
  RCIF(!producer, rc, GR_ERROR_RESOURCE_NOT_FOUND, finish);

  RCB(finish, m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_PAYLOAD,
    .resource_id = producer->transport->router->worker_id,
    .input = {
      .payload       = {
        .type        = WRC_PAYLOAD_PRODUCER_SEND,
        .payload     = payload,
        .payload_len = payload_len
      }
    }
  }));

  if (ppid == 56) {
    m->input.payload.const_payload = " ";
    m->input.payload.payload_len = 1;
  } else if (ppid == 57) {
    m->input.payload.const_payload = ""; // will send one terminating null byte
    m->input.payload.payload_len = 1;
  }

  RCC(rc, finish, jbl_create_empty_object(&m->input.payload.data));
  RCC(rc, finish, jbl_set_int64(m->input.payload.data, "ppid", ppid));
  jbl_clone(producer->identity, &m->input.payload.internal);

  rct_resource_unlock_keep_ref(producer), locked = false;

  rc = wrc_send(m);

finish:
  rct_resource_ref_unlock(producer, locked, -1, __func__);
  return rc;
}
