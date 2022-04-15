#pragma once

#include "rct_producer.h"

iwrc rct_consumer_create(
  wrc_resource_t        transport_id,
  wrc_resource_t        producer_id,
  const char           *rtp_capabilities,
  bool                  paused,
  rct_consumer_layer_t *preferred_layer,
  wrc_resource_t       *consumer_out);

iwrc rct_consumer_create2(
  wrc_resource_t        transport_id,
  wrc_resource_t        producer_id,
  JBL_NODE              rtp_capabilities,
  bool                  paused,
  rct_consumer_layer_t *preferred_layer,
  wrc_resource_t       *consumer_out);

iwrc rct_consumer_set_preferred_layers(wrc_resource_t consumer_id, rct_consumer_layer_t layer);

iwrc rct_consumer_set_priority(wrc_resource_t consumer_id, int priority);

iwrc rct_consumer_unset_priority(wrc_resource_t consumer_id);

iwrc rct_consumer_request_key_frame(wrc_resource_t consumer_id);

iwrc rct_consumer_stats(wrc_resource_t consumer_id, JBL *dump_out);

iwrc rct_producer_can_consume(wrc_resource_t producer_id, const char *rtp_capabilities, bool *out);

iwrc rct_producer_can_consume2(wrc_resource_t producer_id, JBL_NODE rtp_capabilities, bool *out);

iwrc rct_consumer_dump(wrc_resource_t consumer_id, JBL *dump_out);

iwrc rct_consumer_is_paused(wrc_resource_t consumer_id, bool *paused_out);

iwrc rct_consumer_pause(wrc_resource_t consumer_id);

iwrc rct_consumer_resume(wrc_resource_t consumer_id);

#define CONSUMER_TRACE_EVENT_RTP      0x01
#define CONSUMER_TRACE_EVENT_KEYFRAME 0x02
#define CONSUMER_TRACE_EVENT_NACK     0x04
#define CONSUMER_TRACE_EVENT_PLI      0x08
#define CONSUMER_TRACE_EVENT_FIR      0x10

iwrc rct_consumer_enable_trace_events(wrc_resource_t consumer_id, uint32_t events);

iwrc rct_consumer_close(wrc_resource_t consumer_id);

//
// Data consumer specific
//

iwrc rct_consumer_data_create(
  wrc_resource_t  transport_id,
  wrc_resource_t  producer_id,
  int             ordered,
  int             max_packet_lifetime_ms,
  int             max_retransmits_ms,
  wrc_resource_t *consumer_out);
