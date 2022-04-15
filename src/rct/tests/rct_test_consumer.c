#include "rct_tests.h"
#include "rct/rct_consumer.h"
#include "rct_test_consumer.h"
#include "gr_crypt.h"

#include <CUnit/Basic.h>
#include <assert.h>
#include <iwnet/bearssl_hash.h>

static pthread_t poll_in_thr;
static wrc_resource_t worker_id,
                      router_id,
                      transport1_id,
                      transport2_id,
                      audio_producer_id,
                      video_producer_id,
                      audio_consumer_id,
                      video_consumer_id;

static rct_resource_base_t probe, probe2;
static IWPOOL *suite_pool;
static char buf[1024];

static int init_suite(void) {
  suite_pool = iwpool_create(1024);
  if (!suite_pool) {
    return 1;
  }
  iwrc rc = gr_init_noweb(3, (char*[]) {
    "rct_test_consumer",
    "-c",
    "./rct_test_consumer.ini",
    0
  });
  RCGO(rc, finish);

  RCC(rc, finish, iwn_poller_poll_in_thread(g_env.poller, &poll_in_thr));
  RCC(rc, finish, rct_worker_acquire_for_router(&worker_id));
  RCC(rc, finish, rct_router_create(0, worker_id, &router_id));

  rct_transport_webrtc_spec_t *transport_spec1, *transport_spec2;
  RCC(rc, finish, rct_transport_webrtc_spec_create2(0, "127.0.0.1", 0, &transport_spec1));
  RCC(rc, finish, rct_transport_webrtc_spec_create2(0, "127.0.0.1", 0, &transport_spec2));
  RCC(rc, finish, rct_transport_webrtc_create(transport_spec1, router_id, &transport1_id));
  RCC(rc, finish, rct_transport_webrtc_create(transport_spec2, router_id, &transport2_id));

  rct_producer_spec_t *audio_producer_spec, *video_producer_spec;
  RCC(rc, finish, rct_producer_spec_create(RTP_KIND_AUDIO, _data_audio_producer_rtp_params,
                                           &audio_producer_spec));
  RCC(rc, finish, rct_producer_spec_create(RTP_KIND_VIDEO, _data_video_producer_rtp_params,
                                           &video_producer_spec));
  RCC(rc, finish, rct_producer_create(transport1_id, audio_producer_spec, &audio_producer_id));
  RCC(rc, finish, rct_producer_create(transport1_id, video_producer_spec, &video_producer_id));
  RCC(rc, finish, rct_producer_pause(video_producer_id));


  // Add stats handle as LAST RCT listener
  RCC(rc, finish, _rct_event_stats_init());

finish:
  if (rc) {
    _rct_event_stats_destroy();
    iwlog_ecode_error3(rc);
  }
  return rc != 0;
}

static int clean_suite(void) {
  iwrc rc = 0;
  if (worker_id) {
    IWRC(rct_worker_shutdown(worker_id), rc);
  }
  IWRC(gr_shutdown_noweb(), rc);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  _rct_event_stats_destroy();
  iwpool_destroy(suite_pool);
  pthread_join(poll_in_thr, 0);
  return rc != 0;
}

static void test_consumer_close_succeed(void) {
  JBL dump, jbl;
  _rct_reset_awaited_event();

  iwrc rc = rct_consumer_close(audio_consumer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  _rct_await_event(WRC_EVT_CONSUMER_CLOSED);
  rct_consumer_t *c = rct_resource_by_id_locked(audio_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NULL(c);
  rct_resource_unlock(c, __func__);

  rc = rct_resource_probe_by_id(audio_producer_id, &probe);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_router_dump(router_id, &dump);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  sprintf(buf, "/data/mapProducerIdConsumerIds/%s", probe.uuid);
  rc = jbl_at(dump, buf, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl), JBV_ARRAY);
  CU_ASSERT_EQUAL(jbl_count(jbl), 0);

  jbl_destroy(&dump);
  rc = rct_transport_dump(transport2_id, &dump);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = rct_resource_probe_by_id(video_consumer_id, &probe);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  jbl_destroy(&jbl);
  rc = jbl_at(dump, "/data/consumerIds/0", &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl), JBV_STR);
  CU_ASSERT_STRING_EQUAL(jbl_get_str(jbl), probe.uuid);
  jbl_destroy(&jbl);
  rc = jbl_at(dump, "/data/consumerIds/1", &jbl);
  CU_ASSERT(rc != 0);
  jbl_destroy(&jbl);
  rc = jbl_at(dump, "/data/producerIds", &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl), JBV_ARRAY);
  CU_ASSERT_EQUAL(jbl_count(jbl), 0)

  jbl_destroy(&jbl);
  jbl_destroy(&dump);
}

static void test_consumer_emits_pause_resume_events(void) {
  _rct_reset_awaited_event();

  iwrc rc = rct_producer_pause(audio_producer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  _rct_await_event(WRC_EVT_CONSUMER_PRODUCER_PAUSE);

  rct_consumer_t *c = rct_resource_by_id_locked(audio_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);
  CU_ASSERT_FALSE(c->paused);
  CU_ASSERT_TRUE(c->producer->paused);
  rct_resource_unlock(c, __func__);

  _rct_reset_awaited_event();
  rc = rct_producer_resume(audio_producer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  _rct_await_event(WRC_EVT_CONSUMER_PRODUCER_RESUME);

  c = rct_resource_by_id_locked(audio_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);
  CU_ASSERT_FALSE(c->paused);
  CU_ASSERT_FALSE(c->producer->paused);
  rct_resource_unlock(c, __func__);
}

static void test_consumer_enable_trace_event_succeed(void) {
  JBL dump, jbl;

  iwrc rc = rct_consumer_enable_trace_events(audio_consumer_id, CONSUMER_TRACE_EVENT_RTP | CONSUMER_TRACE_EVENT_PLI);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = rct_consumer_dump(audio_consumer_id, &dump);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbl_at(dump, "/data/traceEventTypes", &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL_FATAL(jbl_type(jbl), JBV_STR);
  const char *s = jbl_get_str(jbl);
  CU_ASSERT_STRING_EQUAL(s, "rtp,pli");

  rc = rct_consumer_enable_trace_events(audio_consumer_id, 0);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  jbl_destroy(&dump);
  rc = rct_consumer_dump(audio_consumer_id, &dump);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  jbl_destroy(&jbl);
  rc = jbl_at(dump, "/data/traceEventTypes", &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL_FATAL(jbl_type(jbl), JBV_STR);
  s = jbl_get_str(jbl);
  CU_ASSERT_STRING_EQUAL(s, "");

  jbl_destroy(&jbl);
  jbl_destroy(&dump);
}

static void test_consumer_set_priority_succeed(void) {
  iwrc rc = rct_consumer_set_priority(video_consumer_id, 2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rct_consumer_t *c = rct_resource_by_id_locked(video_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);
  CU_ASSERT_EQUAL(c->priority, 2);
  rct_resource_unlock(c, __func__);
}

static void test_consumer_set_preferred_layers_succeed(void) {
  iwrc rc = rct_consumer_set_preferred_layers(audio_consumer_id, (rct_consumer_layer_t) {
    .spartial = 1,
    .temporal = 1
  });
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_consumer_t *c = rct_resource_by_id_locked(audio_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);
  CU_ASSERT_EQUAL(c->preferred_layer.spartial, -1);
  CU_ASSERT_EQUAL(c->preferred_layer.temporal, -1);
  rct_resource_unlock(c, __func__);

  rc = rct_consumer_set_preferred_layers(video_consumer_id, (rct_consumer_layer_t) {
    .spartial = 2,
    .temporal = 3
  });
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  c = rct_resource_by_id_locked(video_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);
  CU_ASSERT_EQUAL(c->preferred_layer.spartial, 2);
  CU_ASSERT_EQUAL(c->preferred_layer.temporal, 0);
  rct_resource_unlock(c, __func__);
}

static void test_consumer_pause_and_resume_succeed(void) {
  JBL jbl;
  JBL_NODE dump;
  bool bv;
  IWPOOL *pool = iwpool_create(512);
  iwrc rc = rct_consumer_pause(audio_consumer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_consumer_dump(audio_consumer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &dump, false, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  int rci = jbn_path_compare_bool(dump, "/data/paused", true, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = rct_consumer_resume(audio_consumer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_consumer_is_paused(audio_consumer_id, &bv);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_FALSE(bv);


  jbl_destroy(&jbl);
  iwpool_destroy(pool);
}

static void test_consumer_stats_succeed(void) {
  JBL jbl;
  JBL_NODE stats, n1;
  IWPOOL *pool = iwpool_create(512);
  iwrc rc;

  //
  // Audio consumer
  //

  rc = rct_consumer_stats(audio_consumer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &stats, false, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_consumer_t *c = rct_resource_by_id_locked(audio_consumer_id, 0, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);

  int rci = jbn_path_compare_str(stats, "/data/0/type", "outbound-rtp", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_str(stats, "/data/0/kind", "audio", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_str(stats, "/data/0/mimeType", "audio/opus", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(c->rtp_parameters, "/encodings/0/ssrc", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_I64);

  rci = jbn_path_compare_i64(stats, "/data/0/ssrc", n1->vi64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rct_resource_unlock(c, __func__);
  jbl_destroy(&jbl);

  //
  // Video consumer
  //

  rc = rct_consumer_stats(video_consumer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &stats, false, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  c = rct_resource_by_id_locked(video_consumer_id, 0, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);

  rci = jbn_path_compare_str(stats, "/data/0/type", "outbound-rtp", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_str(stats, "/data/0/mimeType", "video/H264", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_i64(stats, "/data/0/score", 10, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(c->rtp_parameters, "/encodings/0/ssrc", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_I64);

  rci = jbn_path_compare_i64(stats, "/data/0/ssrc", n1->vi64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rct_resource_unlock(c, __func__);
  jbl_destroy(&jbl);
  iwpool_destroy(pool);
}

static void test_consumer_dump_succeed(void) {
  JBL jbl;
  JBL_NODE dump, n1, n2;
  rct_consumer_t *c;
  const char *data;
  int rci;

  IWPOOL *pool = iwpool_create(512);

  //
  // AUDIO CONSUMER
  //

  iwrc rc = rct_consumer_dump(audio_consumer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &dump, false, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  c = rct_resource_by_id_locked(audio_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);

  rc = jbn_at(dump, "/data/rtpParameters/codecs/0", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  data = "{"
         "\"channels\": 2,"
         "\"clockRate\": 48000,"
         "\"mimeType\": \"audio/opus\","
         "\"parameters\": {"
         "  \"bar\": \"333\","
         "  \"foo\": 222.222,"
         "  \"usedtx\": 1,"
         "  \"useinbandfec\": 1"
         "},"
         "\"payloadType\": 100,"
         "\"rtcpFeedback\": []"
         "}";

  rc = jbn_from_json(data, &n2, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rci = jbn_compare_nodes(n1, n2, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(dump, "/data/rtpParameters/headerExtensions", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  data = "["
         "{"
         "  \"uri\": \"urn:ietf:params:rtp-hdrext:sdes:mid\","
         "  \"id\": 1,"
         "  \"encrypt\": false,"
         "  \"parameters\": {}"
         "},"
         "{"
         "  \"uri\": \"http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\","
         "  \"id\": 4,"
         "  \"encrypt\": false,"
         "  \"parameters\": {}"
         "},"
         "{"
         "  \"uri\": \"urn:ietf:params:rtp-hdrext:ssrc-audio-level\","
         "  \"id\": 10,"
         "  \"encrypt\": false,"
         "  \"parameters\": {}"
         "}"
         "]";
  rc = jbn_from_json(data, &n2, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rci = jbn_compare_nodes(n1, n2, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(dump, "/data/rtpParameters/encodings", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_ARRAY);
  CU_ASSERT_PTR_NOT_NULL(n1->child);
  CU_ASSERT_PTR_NULL(n1->child->next);

  rc = jbn_at(c->rtp_parameters, "/encodings/0/ssrc", &n2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n2->type, JBV_I64);

  rci = jbn_path_compare_i64(n1->child, "/codecPayloadType", 100, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);
  rci = jbn_path_compare_i64(n1->child, "/ssrc", n2->vi64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_str(dump, "/data/type", "simple", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rct_producer_t *producer = (void*) c->producer;
  CU_ASSERT_EQUAL_FATAL(producer->type, RCT_TYPE_PRODUCER);

  rc = jbn_at(producer->spec->consumable_rtp_parameters, "/encodings/0/ssrc", &n2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n2->type, JBV_I64);

  rci = jbn_path_compare_i64(dump, "/data/consumableRtpEncodings/0/ssrc", n2->vi64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_bool(dump, "/data/paused", false, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_i64(dump, "/data/priority", 1, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rct_resource_unlock(c, __func__);

  //
  // VIDEO CONSUMER
  //

  jbl_destroy(&jbl);
  rc = rct_consumer_dump(video_consumer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &dump, false, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  c = rct_resource_by_id_locked(video_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);

  producer = (void*) c->producer;
  CU_ASSERT_EQUAL_FATAL(producer->type, RCT_TYPE_PRODUCER);

  // jbn_as_json(dump, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
  rci = jbn_path_compare_str(dump, "/data/id", c->uuid, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_str(dump, "/data/producerId", producer->uuid, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  data
    = "["
      "{"
      " \"clockRate\": 90000,"
      " \"mimeType\": \"video/H264\","
      " \"parameters\": {\"packetization-mode\": 1,\"profile-level-id\": \"4d0032\"},"
      " \"payloadType\": 103,"
      " \"rtcpFeedback\": ["
      "   {\"type\": \"nack\"},"
      "   {\"parameter\": \"pli\",\"type\": \"nack\"},"
      "   {\"parameter\": \"fir\",\"type\": \"ccm\"},"
      "   {\"type\": \"goog-remb\"}"
      " ]"
      "},"
      "{"
      " \"clockRate\": 90000,"
      " \"mimeType\": \"video/rtx\","
      " \"parameters\": {\"apt\": 103},"
      " \"payloadType\": 104,"
      " \"rtcpFeedback\": []"
      "}"
      "]";
  rc = jbn_at(dump, "/data/rtpParameters/codecs", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_ARRAY);
  rc = jbn_from_json(data, &n2, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rci = jbn_compare_nodes(n1, n2, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  data
    = "["
      "{\"encrypt\": false,\"id\": 1,\"parameters\": {},\"uri\": \"urn:ietf:params:rtp-hdrext:sdes:mid\"}"
      "{\"encrypt\": false,\"id\": 4,\"parameters\": {},\"uri\": \"http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\"},"
      "{\"encrypt\": false,\"id\": 11,\"parameters\": {},\"uri\": \"urn:3gpp:video-orientation\"},"
      "{\"encrypt\": false,\"id\": 12,\"parameters\": {},\"uri\": \"urn:ietf:params:rtp-hdrext:toffset\"}"
      "]";

  rc = jbn_at(dump, "/data/rtpParameters/headerExtensions", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_ARRAY);
  rc = jbn_from_json(data, &n2, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rci = jbn_compare_nodes(n1, n2, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(dump, "/data/rtpParameters/encodings", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_ARRAY);
  CU_ASSERT_EQUAL(jbn_length(n1), 1);

  rc = jbn_at(dump, "/data/rtpParameters/encodings/0", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rci = jbn_path_compare_i64(n1, "/codecPayloadType", 103, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(c->rtp_parameters, "/encodings/0/ssrc", &n2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n2->type, JBV_I64);
  rci = jbn_path_compare_i64(n1, "/ssrc", n2->vi64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(c->rtp_parameters, "/encodings/0/rtx/ssrc", &n2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n2->type, JBV_I64);
  rci = jbn_path_compare_i64(n1, "/rtx/ssrc", n2->vi64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_str(n1, "/scalabilityMode", "S4T1", &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_i64(n1, "/spatialLayers", 4, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_i64(n1, "/temporalLayers", 1, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_bool(n1, "/ksvc", false, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(dump, "/data/consumableRtpEncodings", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_ARRAY);
  CU_ASSERT_EQUAL(jbn_length(n1), 4);

  rci = jbn_paths_compare(n1, "/0/ssrc", producer->spec->consumable_rtp_parameters, "/encodings/0/ssrc", JBV_I64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_paths_compare(n1, "/1/ssrc", producer->spec->consumable_rtp_parameters, "/encodings/1/ssrc", JBV_I64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_paths_compare(n1, "/2/ssrc", producer->spec->consumable_rtp_parameters, "/encodings/2/ssrc", JBV_I64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_paths_compare(n1, "/3/ssrc", producer->spec->consumable_rtp_parameters, "/encodings/3/ssrc", JBV_I64, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_i64(dump, "/data/supportedCodecPayloadTypes/0", 103, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_bool(dump, "/data/paused", true, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_bool(dump, "/data/producerPaused", true, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rci = jbn_path_compare_i64(dump, "/data/priority", 1, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rct_resource_unlock(c, __func__);
  jbl_destroy(&jbl);
  iwpool_destroy(pool);
}

static void test_transport_consume_inompatible_failed(void) {
  bool bv;
  wrc_resource_t res;

  iwrc rc = rct_producer_can_consume(audio_producer_id, _data_invalid_device_capabilities, &bv);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_FALSE(bv);

  rc = rct_consumer_create(transport2_id,
                           audio_producer_id,
                           _data_invalid_device_capabilities,
                           false, 0, &res);

  CU_ASSERT_EQUAL(rc, RCT_ERROR_INVALID_RTP_PARAMETERS);
}

static void test_transport_consume_succeeds(void) {
  _rct_event_stats_reset(&event_stats);

  int rci;
  bool bv;
  JBL_NODE n1, n2;
  JBL jbl = 0;
  char hash[65];

  IWXSTR *xstr = iwxstr_new();
  CU_ASSERT_PTR_NOT_NULL_FATAL(xstr);

  IWPOOL *pool = iwpool_create(255);
  CU_ASSERT_PTR_NOT_NULL_FATAL(pool);

  iwrc rc = rct_producer_can_consume(audio_producer_id, _data_consumer_device_capabilities, &bv);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(bv, true);

  //
  // Audio consumer
  //
  rc = rct_consumer_create(transport2_id,
                           audio_producer_id,
                           (void*) _data_consumer_device_capabilities,
                           false, 0, &audio_consumer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_consumer_t *c = rct_resource_by_id_locked(audio_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);

  rct_producer_t *producer = (void*) c->producer;
  CU_ASSERT_EQUAL_FATAL(producer->type, RCT_TYPE_PRODUCER);

  CU_ASSERT_EQUAL(producer->id, audio_producer_id);
  CU_ASSERT_EQUAL(producer->spec->rtp_kind, RTP_KIND_AUDIO);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c->rtp_capabilities);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c->rtp_parameters);

  // jbn_as_json(c->rtp_parameters, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);

  rci = jbn_path_compare_str(c->rtp_parameters, "/codecs/0/mimeType", "audio/opus", &rc);
  CU_ASSERT(!rc && !rci);
  rci = jbn_path_compare_i64(c->rtp_parameters, "/codecs/0/payloadType", 100, &rc);
  CU_ASSERT(!rc && !rci);
  rci = jbn_path_compare_i64(c->rtp_parameters, "/codecs/0/parameters/useinbandfec", 1, &rc);
  CU_ASSERT(!rc && !rci);
  rci = jbn_path_compare_str(c->rtp_parameters, "/codecs/0/parameters/bar", "333", &rc);
  CU_ASSERT(!rc && !rci);

  CU_ASSERT_EQUAL(producer->producer_type, RCT_PRODUCER_SIMPLE);
  CU_ASSERT_FALSE(c->paused);
  CU_ASSERT_FALSE(producer->paused);
  CU_ASSERT_EQUAL(c->priority, 1);
  CU_ASSERT_EQUAL(c->score, 10);
  CU_ASSERT_EQUAL(c->producer_score, 0);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c->producer_scores);
  CU_ASSERT_EQUAL(jbl_count(c->producer_scores), 1);

  rct_resource_unlock(c, __func__);

  jbl_destroy(&jbl);
  rc = rct_transport_dump(transport2_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &n1, true, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_resource_probe_by_id(audio_consumer_id, &probe);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT(probe.type & RCT_TYPE_CONSUMER);

  rci = jbn_path_compare_str(n1, "/data/consumerIds/0", probe.uuid, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = jbn_at(n1, "/data/producerIds", &n2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT(n2->type == JBV_ARRAY && !n2->child);

  //
  // Video consumer
  //

  rc = rct_producer_can_consume(video_producer_id, _data_consumer_device_capabilities, &bv);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(bv, true);

  rc = rct_consumer_create(
    transport2_id,
    video_producer_id,
    _data_consumer_device_capabilities,
    true,
    &(rct_consumer_layer_t) {
    12, -1
  },
    &video_consumer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  c = rct_resource_by_id_locked(video_consumer_id, RCT_TYPE_CONSUMER, __func__);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c);

  producer = (void*) c->producer;
  CU_ASSERT_EQUAL_FATAL(producer->type, RCT_TYPE_PRODUCER);
  CU_ASSERT_EQUAL(producer->producer_type, RCT_PRODUCER_SIMULCAST);

  rc = jbn_at(c->rtp_parameters, "/codecs/0", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  iwxstr_clear(xstr);
  jbn_as_json(n1, jbl_xstr_json_printer, xstr, 0);

  //   {
  //   "mimeType": "video/H264",
  //   "clockRate": 90000,
  //   "payloadType": 103,
  //   "parameters": {
  //     "packetization-mode": 1,
  //     "profile-level-id": "4d0032"
  //   },
  //   "rtcpFeedback": [
  //     {
  //       "type": "nack",
  //       "parameter": ""
  //     },
  //     {
  //       "type": "nack",
  //       "parameter": "pli"
  //     },
  //     {
  //       "type": "ccm",
  //       "parameter": "fir"
  //     },
  //     {
  //       "type": "goog-remb",
  //       "parameter": ""
  //     }
  //   ]
  //  }

  gr_crypt_sha256(iwxstr_ptr(xstr), iwxstr_size(xstr), hash);
  CU_ASSERT_STRING_EQUAL(hash, "49b9ebd39418fe5a76bd8925fe3a275a5eaccee67812bcccc1d5c206bdb20e4a");

  // {
  // "mimeType": "video/rtx",
  // "clockRate": 90000,
  // "parameters": {
  //   "apt": 103
  // },
  // "payloadType": 104,
  // "rtcpFeedback": []
  // }
  rc = jbn_at(c->rtp_parameters, "/codecs/1", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  iwxstr_clear(xstr);
  jbn_as_json(n1, jbl_xstr_json_printer, xstr, 0);
  gr_crypt_sha256(iwxstr_ptr(xstr), iwxstr_size(xstr), hash);
  CU_ASSERT_STRING_EQUAL(hash, "3cd63fa177a456c3e8dc4184d823d997e8f3466c4214706f4010525eff33057b");
  CU_ASSERT_EQUAL(producer->producer_type, RCT_PRODUCER_SIMULCAST);
  CU_ASSERT_TRUE(c->paused);
  CU_ASSERT_TRUE(c->producer->paused);
  CU_ASSERT_EQUAL(c->score, 10);
  CU_ASSERT_EQUAL(c->producer_score, 0);

  iwxstr_clear(xstr);
  CU_ASSERT_PTR_NOT_NULL_FATAL(c->producer_scores);
  jbl_as_json(c->producer_scores, jbl_xstr_json_printer, xstr, 0);
  // fprintf(stderr, "%s\n", iwxstr_ptr(xstr));
  CU_ASSERT_STRING_EQUAL(iwxstr_ptr(xstr), "[0,0,0,0]");

  CU_ASSERT_EQUAL(c->preferred_layer.spartial, 3);
  CU_ASSERT_EQUAL(c->preferred_layer.temporal, 0);
  CU_ASSERT_EQUAL(c->current_layer.spartial, -1);
  CU_ASSERT_EQUAL(c->current_layer.temporal, -1);

  rct_resource_unlock(c, __func__);

  jbl_destroy(&jbl);
  rc = rct_router_dump(router_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_to_node(jbl, &n1, true, pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  // {
  //  "accepted": true,
  //  "data": {
  //   "id": "d8bc5edc-5662-4baf-92b6-344c935592af",
  //   "mapConsumerIdProducerId": {
  //    "3f766649-97c1-4977-bec6-1ae26250dcf3": "519166e0-51c4-4c68-be5c-e9fbf7470a66",
  //    "6214c2d8-5697-4f37-a0db-37d7b3e50ff4": "06214212-1dd7-4643-90ad-a471559edf94"
  //   },
  //   "mapDataConsumerIdDataProducerId": {},
  //   "mapDataProducerIdDataConsumerIds": {},
  //   "mapProducerIdConsumerIds": {
  //    "06214212-1dd7-4643-90ad-a471559edf94": [
  //     "6214c2d8-5697-4f37-a0db-37d7b3e50ff4"
  //    ],
  //    "519166e0-51c4-4c68-be5c-e9fbf7470a66": [
  //     "3f766649-97c1-4977-bec6-1ae26250dcf3"
  //    ]
  //   },
  //   "mapProducerIdObserverIds": {
  //    "06214212-1dd7-4643-90ad-a471559edf94": [],
  //    "519166e0-51c4-4c68-be5c-e9fbf7470a66": []
  //   },
  //   "rtpObserverIds": [],
  //   "transportIds": [
  //    "f9f00c51-2693-47d8-ba8c-07709b3bccba",
  //    "01a5f8fa-0e68-41df-a731-cb8835924cf1"
  //   ]
  //  },
  //  "id": 1
  // }

  rc = rct_resource_probe_by_id(audio_producer_id, &probe);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = rct_resource_probe_by_id(audio_consumer_id, &probe2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  sprintf(buf, "/data/mapProducerIdConsumerIds/%s/0", probe.uuid);
  rci = jbn_path_compare_str(n1, buf, probe2.uuid, &rc);
  CU_ASSERT_EQUAL(rci, 0);

  sprintf(buf, "/data/mapConsumerIdProducerId/%s", probe2.uuid);
  rci = jbn_path_compare_str(n1, buf, probe.uuid, &rc);
  CU_ASSERT_EQUAL(rci, 0);

  rc = rct_resource_probe_by_id(video_producer_id, &probe);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = rct_resource_probe_by_id(video_consumer_id, &probe2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  sprintf(buf, "/data/mapProducerIdConsumerIds/%s/0", probe.uuid);
  rci = jbn_path_compare_str(n1, buf, probe2.uuid, &rc);
  CU_ASSERT_EQUAL(rci, 0);

  sprintf(buf, "/data/mapConsumerIdProducerId/%s", probe2.uuid);
  rci = jbn_path_compare_str(n1, buf, probe.uuid, &rc);
  CU_ASSERT_EQUAL(rci, 0);

  jbl_destroy(&jbl);
  iwxstr_destroy(xstr);
  iwpool_destroy(pool);
}

int main(int argc, char *argv[]) {
  int rv = 0;
  if (gr_exec_embedded(argc, argv, &rv)) {
    return rv;
  }

  CU_pSuite pSuite = NULL;
  if (CUE_SUCCESS != CU_initialize_registry()) {
    return CU_get_error();
  }
  pSuite = CU_add_suite("rct_test_consumer", init_suite, clean_suite);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  if (  (NULL == CU_add_test(pSuite, "transport.consume() succeeds", test_transport_consume_succeeds))
     || (NULL == CU_add_test(pSuite, "transport.consume() with incompatible rtpCapabilities failed",
                             test_transport_consume_inompatible_failed))
     || (NULL == CU_add_test(pSuite, "consumer.dump() succeeds", test_consumer_dump_succeed))
     || (NULL == CU_add_test(pSuite, "consumer.getStats() succeeds", test_consumer_stats_succeed))
     || (NULL == CU_add_test(pSuite, "consumer.pause() and resume() succeed", test_consumer_pause_and_resume_succeed))
     || (NULL ==
         CU_add_test(pSuite, "consumer.setPreferredLayers() succeed", test_consumer_set_preferred_layers_succeed))
     || (NULL == CU_add_test(pSuite, "consumer.setPriority() succeed", test_consumer_set_priority_succeed))
     || (NULL == CU_add_test(pSuite, "consumer.enableTraceEvent() succeed", test_consumer_enable_trace_event_succeed))
     || (NULL ==
         CU_add_test(pSuite, "consumer emits producerpause/producerresume", test_consumer_emits_pause_resume_events))
     || (NULL == CU_add_test(pSuite, "consumer.close() succeeds", test_consumer_close_succeed))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
