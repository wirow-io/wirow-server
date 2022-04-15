#pragma once

#include "rct_transport.h"

#define RTP_KIND_ALL   0x03
#define RTP_KIND_VIDEO 0x01
#define RTP_KIND_AUDIO 0x02

iwrc rct_producer_spec_create(uint32_t rtp_kind, const char *rtp_params_json, rct_producer_spec_t **spec_output);

iwrc rct_producer_spec_create2(uint32_t rtp_kind, JBL_NODE rtp_params, rct_producer_spec_t **spec_output);

iwrc rct_producer_create(wrc_resource_t transport_id, rct_producer_spec_t *spec, wrc_resource_t *producer_out);

iwrc rct_producer_dump(wrc_resource_t producer_id, JBL *dump_out);

iwrc rct_producer_stats(wrc_resource_t producer_id, JBL *result_out);

iwrc rct_producer_pause(wrc_resource_t producer_id);

iwrc rct_producer_pause_async(wrc_resource_t producer_id);

iwrc rct_producer_resume(wrc_resource_t producer_id);

iwrc rct_producer_resume_async(wrc_resource_t producer_id);

JBL_NODE rct_codec_find_matched(JBL_NODE codec, JBL_NODE codecs, bool strict, bool modify, IWPOOL *pool);

#define PRODUCER_TRACE_EVENT_RTP      0x01
#define PRODUCER_TRACE_EVENT_KEYFRAME 0x02
#define PRODUCER_TRACE_EVENT_NACK     0x04
#define PRODUCER_TRACE_EVENT_PLI      0x08
#define PRODUCER_TRACE_EVENT_FIR      0x10

iwrc rct_producer_enable_trace_events(wrc_resource_t producer_id, uint32_t events);

iwrc rct_producer_close(wrc_resource_t producer_id);

//
// Data producer specific
//

iwrc rct_producer_data_spec_create(
  const char                *sctp_stream_parameters,
  const char                *label,
  const char                *protocol,
  rct_producer_data_spec_t **spec_out);

iwrc rct_producer_data_create(
  wrc_resource_t                  transport_id,
  const rct_producer_data_spec_t *spec,
  wrc_resource_t                 *producer_out);

iwrc rct_producer_data_close(wrc_resource_t producer_id);

iwrc rct_producer_data_send(
  wrc_resource_t producer_id,
  char *payload, size_t payload_len,
  bool binary_message, int32_t ppid);

//
// Direct transport specific
//

iwrc rct_producer_direct_send_rtp_packet(wrc_resource_t producer_id, char *payload, size_t payload_len);
