#include "rct_tests.h"
#include "wrc/wrc.h"
#include "rct/rct_transport.h"
#include "rct/rct_producer.h"
#include "rct/rct_consumer.h"

#include <CUnit/Basic.h>
#include <assert.h>

static pthread_t poll_in_thr;
static wrc_resource_t worker_id,
                      router_id,
                      transport_id,
                      transport1_id;

static IWPOOL *suite_pool;

static int init_suite(void) {
  suite_pool = iwpool_create(1024);
  if (!suite_pool) {
    return 1;
  }
  iwrc rc = gr_init_noweb(3, (char*[]) {
    "test_transport_plain",
    "-c",
    "./rct_test_consumer.ini",
    0
  });
  RCGO(rc, finish);

  RCC(rc, finish, iwn_poller_poll_in_thread(g_env.poller, &poll_in_thr));
  RCC(rc, finish, rct_worker_acquire_for_router(&worker_id));
  RCC(rc, finish, rct_router_create(0, worker_id, &router_id));

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

static void before_test(void) {
  rct_transport_plain_spec_t *spec = 0;
  iwrc rc = rct_transport_plain_spec_create(
    "127.0.0.1",
    "4.4.4.4",
    &spec);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_PTR_NOT_NULL_FATAL(spec);
  spec->no_mux = true;

  rc = rct_transport_plain_create(spec, router_id, &transport_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
}

static void after_test(void) {
  rct_transport_close(transport_id);
  transport_id = 0;
}

static void rct_transport_plain_srtp_create(void) {
  _rct_event_stats_reset(&event_stats);

  JBL_NODE n1;
  rct_transport_connect_t *conn;
  rct_transport_plain_spec_t *spec = 0;
  iwrc rc = rct_transport_plain_spec_create("127.0.0.1", 0, &spec);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  spec->enable_srtp = true;

  rc = rct_transport_plain_create(spec, router_id, &transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_transport_plain_t *transport = rct_resource_by_id_unsafe(transport1_id, RCT_TYPE_TRANSPORT_PLAIN);
  CU_ASSERT_PTR_NOT_NULL_FATAL(transport);
  CU_ASSERT_PTR_NOT_NULL_FATAL(transport->data);

  rc = jbn_at(transport->data, "/data/srtpParameters/cryptoSuite", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, "AES_CM_128_HMAC_SHA1_80");

  rc = rct_transport_plain_connect_spec_create("127.0.0.2", "ZnQ3eWJraDg0d3ZoYzM5cXN1Y2pnaHU5NWxrZTVv", &conn);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  conn->type = RCT_TYPE_TRANSPORT_PLAIN;
  conn->plain.port = 9999;
  conn->plain.crypto_suite = RCT_AES_CM_128_HMAC_SHA1_32;

  rc = rct_transport_connect(transport1_id, conn);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbn_at(transport->data, "/data/srtpParameters/cryptoSuite", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, "AES_CM_128_HMAC_SHA1_32");

  rc = rct_transport_close(transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
}

static void test_transport_plain_create(void) {
  _rct_event_stats_reset(&event_stats);

  JBL jbl = 0;
  JBL_NODE n1, n2, n3;
  rct_transport_plain_spec_t *spec = 0;
  iwrc rc = rct_transport_plain_spec_create(
    "127.0.0.1",
    "9.9.9.1",
    &spec);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_PTR_NOT_NULL_FATAL(spec);
  spec->enable_sctp = true;
  _rct_await_event(WRC_EVT_TRANSPORT_CREATED);

  rc = rct_transport_plain_create(spec, router_id, &transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_transport_plain_t *transport = rct_resource_by_id_unsafe(transport1_id, RCT_TYPE_TRANSPORT_PLAIN);
  CU_ASSERT_PTR_NOT_NULL_FATAL(transport);
  CU_ASSERT_PTR_NOT_NULL_FATAL(transport->data);

  rc = jbn_at(transport->data, "/data/sctpParameters", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);


  const char *json = "{"
                     "\"port\":  5000,"
                     "\"OS\":    1024,"
                     "\"isDataChannel\": false,"
                     "\"MIS\":   1024,"
                     "\"maxMessageSize\": 262144,"
                     "\"sctpBufferedAmount\": 0,"
                     "\"sendBufferSize\": 262144"
                     "}";
  rc = jbn_from_json(json, &n2, suite_pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  int rvi = jbn_compare_nodes(n1, n2, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rvi, 0);

  rc = jbn_at(transport->data, "/data/tuple/localIp", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, "9.9.9.1");

  rc = jbn_at(transport->data, "/data/tuple/protocol", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, "udp");

  rc = jbn_at(transport->data, "/data/sctpState", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, "new");

  rc = jbn_at(transport->data, "/data/comedia", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_BOOL);
  CU_ASSERT_EQUAL(n1->vbool, false);

  rc = rct_transport_dump(transport1_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbl_to_node(jbl, &n1, false, suite_pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbn_at(n1, "/data/sctpParameters", &n3);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rvi = jbn_compare_nodes(n2, n3, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rvi, 0);

  rc = rct_transport_close(transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  jbl_destroy(&jbl);
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
  pSuite = CU_add_suite_with_setup_and_teardown("test_transport_plain",
                                                init_suite, clean_suite, before_test, after_test);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  if (  (NULL == CU_add_test(pSuite, "rct_transport_plain_create() succeeds", test_transport_plain_create))
     || (NULL == CU_add_test(pSuite, "rct_transport_plain_srtp_create() succeeds", rct_transport_plain_srtp_create))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
