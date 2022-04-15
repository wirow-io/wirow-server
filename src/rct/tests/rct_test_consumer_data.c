#include "rct_tests.h"
#include "wrc/wrc.h"
#include "rct/rct_transport.h"
#include "rct/rct_producer.h"
#include "rct/rct_consumer.h"
#include <ejdb2/iowow/iwth.h>
#include <ejdb2/iowow/iwconv.h>
#include <pthread.h>

#include <CUnit/Basic.h>
#include <assert.h>

static char buf[1024];

static pthread_t poll_in_thr;
static wrc_resource_t worker_id,
                      router_id,
                      transport1_id,
                      transport2_id,
                      transport3_id,
                      data_producer_id,
                      data_consumer1_id,
                      data_consumer2_id;

static IWPOOL *suite_pool;

static int init_suite(void) {
  suite_pool = iwpool_create(1024);
  if (!suite_pool) {
    return 1;
  }
  iwrc rc = gr_init_noweb(3, (char*[]) {
    "test_consumer_data",
    "-c",
    "./rct_test_consumer.ini",
    0
  });
  RCGO(rc, finish);

  RCC(rc, finish, iwn_poller_poll_in_thread(g_env.poller, &poll_in_thr));
  RCC(rc, finish, rct_worker_acquire_for_router(&worker_id));
  RCC(rc, finish, rct_router_create(0, worker_id, &router_id));

  rct_transport_webrtc_spec_t *spec_webrtc;
  RCC(rc, finish, rct_transport_webrtc_spec_create(RCT_WEBRTC_DEFAULT_FLAGS | RCT_TRN_ENABLE_SCTP, &spec_webrtc));
  RCC(rc, finish, rct_transport_webrtc_spec_add_ip(spec_webrtc, "127.0.0.1", 0));
  RCC(rc, finish, rct_transport_webrtc_create(spec_webrtc, router_id, &transport1_id));

  rct_transport_plain_spec_t *spec_plain;
  RCC(rc, finish, rct_transport_plain_spec_create("127.0.0.1", 0, &spec_plain));
  spec_plain->enable_sctp = true;
  RCC(rc, finish, rct_transport_plain_create(spec_plain, router_id, &transport2_id));

  RCC(rc, finish, rct_transport_direct_create(router_id, 0, &transport3_id));

  rct_producer_data_spec_t *spec_producer_data;
  RCC(rc, finish, rct_producer_data_spec_create(
        "{"
        "\"streamId\": 12345,"
        "\"ordered\": false,"
        "\"maxPacketLifeTime\": 5000"
        "}",
        "foo",
        "bar",
        &spec_producer_data));
  RCC(rc, finish, rct_producer_data_create(transport1_id, spec_producer_data, &data_producer_id));


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
}

static void after_test(void) {
}

static void test_consumer_data_on_direct_transport(void) {

  rct_transport_direct_t *transport3 = rct_resource_by_id_unsafe(transport3_id, RCT_TYPE_TRANSPORT_DIRECT);
  CU_ASSERT_PTR_NOT_NULL_FATAL(transport3);

  iwrc rc = rct_consumer_data_create(transport3_id, data_producer_id, 0, 0, 0, &data_consumer2_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_consumer_data_t *data_consumer2 = rct_resource_by_id_unsafe(data_consumer2_id, RCT_TYPE_CONSUMER_DATA);
  CU_ASSERT_PTR_NOT_NULL_FATAL(data_consumer2);

  CU_ASSERT_EQUAL(data_consumer2->data_type, RCT_DATA_DIRECT);
  CU_ASSERT_STRING_EQUAL(data_consumer2->label, "foo");
  CU_ASSERT_STRING_EQUAL(data_consumer2->protocol, "bar");
}

static void test_consumer_data_stats(void) {
  JBL jbl;

  rct_consumer_data_t *data_consumer1 = rct_resource_by_id_unsafe(data_consumer1_id, RCT_TYPE_CONSUMER_DATA);
  CU_ASSERT_PTR_NOT_NULL_FATAL(data_consumer1);

  iwrc rc = rct_consumer_stats(data_consumer1_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  jbl_destroy(&jbl);
}

static void test_consumer_data_dump(void) {

  JBL jbl = 0;
  JBL_NODE dump, n1;

  rct_consumer_data_t *data_consumer1 = rct_resource_by_id_unsafe(data_consumer1_id, RCT_TYPE_CONSUMER_DATA);
  CU_ASSERT_PTR_NOT_NULL_FATAL(data_consumer1);

  iwrc rc = rct_consumer_dump(data_consumer1_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbl_to_node(jbl, &dump, true, suite_pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  jbl_destroy(&jbl);

  rc = jbn_at(dump, "/data/dataProducerId", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, data_consumer1->producer->uuid);

  rc = jbn_at(dump, "/data/type", &n1);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(n1->type, JBV_STR);
  CU_ASSERT_STRING_EQUAL(n1->vptr, "sctp");

  jbl_destroy(&jbl);
}

static void test_consumer_data_succeeds(void) {

  JBL_NODE n1;
  JBL jbl = 0, jbl2 = 0;

  _rct_reset_awaited_event();
  iwrc rc = rct_consumer_data_create(transport2_id, data_producer_id, 0, 4000, 0, &data_consumer1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  _rct_await_event(WRC_EVT_CONSUMER_CREATED);

  rct_consumer_data_t *data_consumer1 = rct_resource_by_id_unsafe(data_consumer1_id, RCT_TYPE_CONSUMER_DATA);
  CU_ASSERT_PTR_NOT_NULL_FATAL(data_consumer1);

  CU_ASSERT_STRING_EQUAL(data_consumer1->label, "foo");
  CU_ASSERT_STRING_EQUAL(data_consumer1->protocol, "bar");
  CU_ASSERT_EQUAL(data_consumer1->data_type, RCT_DATA_SCTP);

  const char *data = "{"
                     "\"maxPacketLifeTime\": 4000,"
                     "\"ordered\": false,"
                     "\"streamId\": 0"
                     "}";
  rc = jbn_from_json(data, &n1, suite_pool);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  int rci = jbn_compare_nodes(data_consumer1->sctp_stream_parameters, n1, &rc);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(rci, 0);

  rc = rct_router_dump(router_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rct_producer_data_t *data_producer = rct_resource_by_id_unsafe(data_producer_id, 0);
  CU_ASSERT_PTR_NOT_NULL_FATAL(data_producer);

  sprintf(buf, "/data/mapDataProducerIdDataConsumerIds/%s/0", data_producer->uuid);
  rc = jbl_at(jbl, buf, &jbl2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl2), JBV_STR);
  CU_ASSERT_STRING_EQUAL(jbl_get_str(jbl2), data_consumer1->uuid);
  jbl_destroy(&jbl2);

  sprintf(buf, "/data/mapDataConsumerIdDataProducerId/%s", data_consumer1->uuid);
  rc = jbl_at(jbl, buf, &jbl2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl2), JBV_STR);
  CU_ASSERT_STRING_EQUAL(jbl_get_str(jbl2), data_producer->uuid);

  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);

  rc = rct_transport_dump(transport2_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbl_at(jbl, "/data/dataConsumerIds/0", &jbl2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl2), JBV_STR);
  CU_ASSERT_STRING_EQUAL(jbl_get_str(jbl2), data_consumer1->uuid);

  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
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
  pSuite = CU_add_suite_with_setup_and_teardown("test_consumer_data",
                                                init_suite, clean_suite, before_test, after_test);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  if (  (NULL == CU_add_test(pSuite, "rct_test_consumer_data_succeeds", test_consumer_data_succeeds))
     || (NULL == CU_add_test(pSuite, "rct_test_consumer_data_dump", test_consumer_data_dump))
     || (NULL == CU_add_test(pSuite, "rct_test_consumer_data_stats", test_consumer_data_stats))
     || (NULL ==
         CU_add_test(pSuite, "rct_test_consumer_data_on_direct_transport", test_consumer_data_on_direct_transport))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
