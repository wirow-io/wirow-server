#include "rct/rct_consumer.h"
#include <ejdb2/jbl.h>
#include <pthread.h>
#include <CUnit/Basic.h>
#include "rct_test_transport_webrtc.h"

static pthread_t poll_in_thr;

static int init_suite(void) {
  iwrc rc = gr_init_noweb(3, (char*[]) {
    "rct_test2",
    "-c",
    "./rct_test2.ini",
    0
  });
  if (rc) {
    iwlog_ecode_error3(rc);
    return 1;
  }
  rc = iwn_poller_poll_in_thread(g_env.poller, &poll_in_thr);
  return rc != 0;
}

static int clean_suite(void) {
  iwrc rc = gr_shutdown_noweb();
  pthread_join(poll_in_thr, 0);
  return rc != 0;
}

static iwrc _test_transport_connect(wrc_resource_t transport) {
  iwrc rc = 0;
  IWPOOL *pool = iwpool_create(127);
  rct_transport_connect_t spec = {
    .pool   = pool,
    .type   = RCT_TYPE_TRANSPORT_WEBRTC,
    .wbrtc  = {
      .role = "client"
    }
  };
  struct rct_dtls_fp *fp = iwpool_alloc(sizeof(*fp), pool);
  *fp = (struct rct_dtls_fp) {
    .algorithm = RCT_SHA256,
    .value = "82:5A:68:3D:36:C3:0A:DE:AF:E7:32:43:D2:88:83:57:AC:2D:65:E5:80:C4:B6:FB:AF:1A:A0:21:9F:6D:0C:AD"
  };
  spec.wbrtc.fingerprints = fp;
  rc = rct_transport_connect(transport, &spec);
  return rc;
}

static void test2_1(void) {
  iwrc rc = 0;
  wrc_resource_t worker_id = 0, router_id = 0, transport_id = 0, producer_id = 0;
  wrc_msg_t *m = 0;
  JBL jbl = 0, jbl2 = 0;
  rct_resource_base_t h;
  bool bv = false;
  int64_t iv = 0;
  int rv;

  rc = wrc_worker_acquire(&worker_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  IWXSTR *xstr = iwxstr_new();
  CU_ASSERT_PTR_NOT_NULL_FATAL(xstr);

  // Execute worker.dump
  m = wrc_msg_create(&(wrc_msg_t) {
    .type = WRC_MSG_WORKER,
    .resource_id = worker_id,
    .input = {
      .worker = {
        .cmd  = WRC_CMD_WORKER_DUMP
      }
    }
  });
  rc = wrc_send_and_wait(m, 0);

  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_PTR_NOT_NULL_FATAL(m->output.worker.data);
  rc = jbl_object_get_bool(m->output.worker.data, "accepted", &bv);
  CU_ASSERT_EQUAL(rc, 0);
  rc = jbl_object_get_i64(m->output.worker.data, "id", &iv);
  CU_ASSERT_EQUAL(rc, 0);

  rc = rct_router_create("aff5ff57-22dc-4563-a924-59cd88cba3c4", 0, &router_id);
  CU_ASSERT_EQUAL(rc, 0);

  jbl_destroy(&jbl);
  rc = rct_worker_dump(worker_id, &jbl);
  CU_ASSERT_EQUAL(rc, 0);

  rc = jbl_object_get_bool(jbl, "accepted", &bv);
  CU_ASSERT_EQUAL(rc, 0);
  rc = jbl_object_get_i64(jbl, "id", &iv);
  CU_ASSERT_EQUAL(rc, 0);

  jbl_destroy(&jbl2);
  rc = jbl_at(jbl, "/data/routerIds/0", &jbl2);
  CU_ASSERT_EQUAL(rc, 0);

  rv = strcmp("aff5ff57-22dc-4563-a924-59cd88cba3c4", jbl_get_str(jbl2));
  CU_ASSERT(!rv);

  jbl_destroy(&jbl);
  rc = rct_router_dump(router_id, &jbl);
  CU_ASSERT_EQUAL(rc, 0);
  fprintf(stderr, "\n========= Router dump\n");
  jbl_as_json(jbl, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
  fprintf(stderr, "\n========= Router dump\n");

  rct_transport_webrtc_spec_t *wspec;
  rc = rct_transport_webrtc_spec_create(RCT_TRN_ENABLE_UDP
                                        | RCT_TRN_ENABLE_TCP
                                        | RCT_TRN_ENABLE_SCTP
                                        | RCT_TRN_ENABLE_DATA_CHANNEL
                                        | RCT_TRN_PREFER_UDP, &wspec);
  CU_ASSERT_EQUAL(rc, 0);

  rc = rct_transport_webrtc_spec_add_ip(wspec, "127.0.0.1", "9.9.9.1");
  CU_ASSERT_EQUAL(rc, 0);

  rc = rct_transport_webrtc_spec_add_ip(wspec, "0.0.0.0", "9.9.9.2");
  CU_ASSERT_EQUAL(rc, 0);

  rc = rct_transport_webrtc_spec_add_ip(wspec, "127.0.0.1", 0);
  CU_ASSERT_EQUAL(rc, 0);

  wspec->sctp.streams.mis = 2048;
  wspec->sctp.streams.os = 2048;
  wspec->sctp.max_message_size = 1000000;

  rc = rct_transport_webrtc_create(wspec, router_id, &transport_id);
  CU_ASSERT_EQUAL(rc, 0);

  jbl_destroy(&jbl);
  rc = rct_transport_dump(transport_id, &jbl);
  CU_ASSERT_EQUAL(rc, 0);
  fprintf(stderr, "\n========= Transport dump\n");
  jbl_as_json(jbl, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
  fprintf(stderr, "\n========= Transport dump\n");

  // Transport connect
  rc = _test_transport_connect(transport_id);
  CU_ASSERT_EQUAL(rc, 0);

  rc = rct_transport_set_max_incoming_bitrate(transport_id, 100000);
  CU_ASSERT_EQUAL(rc, 0);

  rct_producer_spec_t *spec;
  rc = rct_producer_spec_create(RTP_KIND_AUDIO, (void*) _data_producer_audio_rtp_params, &spec);
  CU_ASSERT_EQUAL(rc, 0);

  rc = rct_producer_create(transport_id, spec, &producer_id);
  CU_ASSERT_EQUAL(rc, 0);

  jbl_destroy(&jbl);
  rc = rct_producer_dump(producer_id, &jbl);
  fprintf(stderr, "\n========= Producer dump\n");
  jbl_as_json(jbl, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
  fprintf(stderr, "\n========= Producer dump\n");

  rc = rct_transport_close(transport_id);
  CU_ASSERT_EQUAL(rc, 0);

  // Close router
  rc = rct_resource_probe_by_uuid("aff5ff57-22dc-4563-a924-59cd88cba3c4", &h);
  CU_ASSERT_EQUAL(rc, 0);
  rc = rct_router_close(h.id);
  CU_ASSERT_EQUAL(rc, 0);


  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  rc = rct_worker_dump(worker_id, &jbl2);
  CU_ASSERT_EQUAL(rc, 0);
  rc = jbl_at(jbl2, "/data/routerIds/0", &jbl);
  CU_ASSERT(rc);
  rc = 0;

  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  iwxstr_destroy(xstr);
  wrc_msg_destroy(m);
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
  pSuite = CU_add_suite("rct_test2", init_suite, clean_suite);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  if (

    (NULL == CU_add_test(pSuite, "test2_1", test2_1))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
