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

static pthread_t poll_in_thr;
static wrc_resource_t worker_id,
                      router_id,
                      transport_id,
                      transport1_id,
                      transport2_id,
                      producer_id,
                      consumer_id;

static IWPOOL *suite_pool;

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
  iwrc rc = rct_transport_direct_create(router_id, 0, &transport_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
}

static void after_test(void) {
  rct_transport_close(transport_id);
}

static void test_transport_direct_create(void) {
  JBL dump, jbl;
  CU_ASSERT_TRUE(router_id > 0);

  _rct_reset_awaited_event();
  iwrc rc = rct_transport_direct_create(router_id, 1024, &transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  _rct_await_event(WRC_EVT_TRANSPORT_CREATED);

  rc = rct_transport_dump(transport1_id, &dump);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbl_at(dump, "/data/maxMessageSize", &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl), JBV_I64);
  CU_ASSERT_EQUAL(jbl_get_i64(jbl), 1024);

  _rct_reset_awaited_event();

  rc = rct_transport_close(transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  _rct_await_event(WRC_EVT_TRANSPORT_CLOSED);

  CU_ASSERT_EQUAL(_rct_event_stats_count(&event_stats, WRC_EVT_TRANSPORT_CREATED), 2);
  CU_ASSERT_EQUAL(_rct_event_stats_count(&event_stats, WRC_EVT_TRANSPORT_CLOSED), 1);

  rc = rct_transport_direct_create(router_id, 0, &transport1_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  jbl_destroy(&dump);
  jbl_destroy(&jbl);
}

static void test_transport_direct_get_stats(void) {
  JBL stats, jbl;
  iwrc rc = rct_transport_stats(transport_id, &stats);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = jbl_at(stats, "/data/0/probationSendBitrate", &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL(jbl_type(jbl), JBV_I64);

  jbl_destroy(&stats);
  jbl_destroy(&jbl);
}

static int num_messages = 200;
pthread_cond_t reciever_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t reciever_mtx = PTHREAD_MUTEX_INITIALIZER;

static void *sender_th(void *op) {
  iwrc rc = 0;
  char msg[128];
  for (int i = 0; i < num_messages; ++i) {
    snprintf(msg, sizeof(msg), "%d", i);
    RCC(rc, finish, rct_producer_data_send(producer_id, strdup(msg), strlen(msg), i >= num_messages / 2, 0));
  }

finish:
  if (rc) {
    return (void*) rc;
  }
  return 0;
}

// TODO: Review!!!
static iwrc on_send_data_event(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  if (evt != WRC_EVT_PAYLOAD) {
    return 0;
  }

  static atomic_int received_messages;

  iwrc rc = 0;
  JBL jbl = 0;
  int64_t msg_id = 0;
  rct_resource_base_t b;
  int64_t ppid = 0;
  const char *target_id = 0;
  uint32_t payload_len = 0;

  if (!data) {
    iwlog_error2("No payload event data");
    rc = IW_ERROR_ASSERTION;
    goto finish;
  }

  char *payload = jbl_get_user_data(data);
  if (!payload) {
    iwlog_error2("No payload received");
    rc = IW_ERROR_ASSERTION;
    goto finish;
  }

  memcpy(&payload_len, payload, 4);
  payload += 4;

  RCC(rc, finish, jbl_object_get_str(data, "targetId", &target_id));
  if (!target_id) {
    iwlog_error2("No /targetId in data");
    goto finish;
  }

  RCC(rc, finish, jbl_at(data, "/data/ppid", &jbl));
  ppid = jbl_get_i64(jbl);

  RCC(rc, finish, rct_resource_probe_by_uuid(target_id, &b));
  if (b.type != RCT_TYPE_CONSUMER_DATA) {
    iwlog_error2("Target must be of RCT_TYPE_CONSUMER_DATA type");
    rc = IW_ERROR_ASSERTION;
    goto finish;
  }

  msg_id = iwatof(payload);
  if (msg_id < num_messages / 2) {
    if (ppid != 51) {
      iwlog_error2("Invalid ppid, should be 51");
      rc = IW_ERROR_ASSERTION;
      goto finish;
    }
  } else {
    if (ppid != 53) {
      iwlog_error2("Invalid ppid, should be 53");
      rc = IW_ERROR_ASSERTION;
      goto finish;
    }
  }
  ++received_messages;
  if (received_messages == num_messages) {
finish:
    if (rc) {
      iwlog_ecode_error3(rc);
    }
    pthread_mutex_lock(&reciever_mtx);
    pthread_cond_broadcast(&reciever_cond);
    pthread_mutex_unlock(&reciever_mtx);
  }
  jbl_destroy(&jbl);
  return 0;
}

static void test_transport_direct_producer_send(void) {

  static pthread_t sender_thread;

  void *res = 0;
  wrc_event_handler_t eh;
  JBL jbl = 0;
  JBL jbl2 = 0;

  iwrc rc = rct_transport_direct_create(router_id, 0, &transport2_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);


  rct_producer_data_spec_t *spec;
  rc = rct_producer_data_spec_create(0, "foo", "bar", &spec);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_producer_data_create(transport2_id, spec, &producer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_consumer_data_create(transport2_id, producer_id, 0, 0, 0, &consumer_id);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  pthread_mutex_lock(&reciever_mtx);
  pthread_create(&sender_thread, 0, sender_th, 0);
  rc = wrc_add_event_handler(on_send_data_event, 0, &eh);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  // Waiting for all pending events
  pthread_cond_wait(&reciever_cond, &reciever_mtx);
  pthread_mutex_unlock(&reciever_mtx);

  pthread_join(sender_thread, &res);

  rc = wrc_remove_event_handler(eh);
  CU_ASSERT_EQUAL_FATAL(rc, 0);

  rc = rct_producer_stats(producer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  rc = jbl_at(jbl, "/data/0/messagesReceived", &jbl2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL_FATAL(jbl_type(jbl2), JBV_I64);
  CU_ASSERT_EQUAL(jbl_get_i64(jbl2), 200);

  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);

  rc = rct_consumer_stats(consumer_id, &jbl);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  jbl_as_json(jbl, jbl_fstream_json_printer, stderr, JBL_PRINT_PRETTY);
  rc = jbl_at(jbl, "/data/0/messagesSent", &jbl2);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  CU_ASSERT_EQUAL_FATAL(jbl_type(jbl2), JBV_I64);
  CU_ASSERT_EQUAL(jbl_get_i64(jbl2), 200);

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
  pSuite = CU_add_suite_with_setup_and_teardown("test_transport_direct",
                                                init_suite, clean_suite, before_test, after_test);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  if (  (NULL == CU_add_test(pSuite, "rct_transport_direct_create() succeeds", test_transport_direct_create))
     || (NULL == CU_add_test(pSuite, "rct_transport_stats() succeeds", test_transport_direct_get_stats))
     || (NULL ==
         CU_add_test(pSuite, "rct_transport_data_producer send succeeds", test_transport_direct_producer_send))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
