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

// todo: Implementation

static int init_suite(void) {
  iwrc rc = 0;

  return rc != 0;
}

static int clean_suite(void) {
  iwrc rc = 0;


  return rc != 0;
}

static void before_test(void) {
}

static void after_test(void) {
}

static void test_pipe_router_succeeds_with_audio(void) {
  // todo: Implementation
}

int main(int argc, char const *argv[]) {
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

  if (NULL == CU_add_test(pSuite, "rct_test_pipe_router_succeeds_with_audio",
                          test_pipe_router_succeeds_with_audio)) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
