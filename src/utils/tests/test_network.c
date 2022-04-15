#include "utils/network.h"
#include <CUnit/Basic.h>

#include <stdlib.h>

static int init_suite(void) {
  return 0;
}

static int clean_suite(void) {
  return 0;
}

static void before_test(void) {
}

static void after_test(void) {
}

static void test_unused_port(void) {
  uint16_t port = network_detect_unused_port(0);
  CU_ASSERT_TRUE(port > 1024);
  port = network_detect_unused_port("127.0.0.1");
  CU_ASSERT_TRUE(port > 1024);
  port = network_detect_unused_port("zzz");
  CU_ASSERT_EQUAL(port, 0);
}

int main(int argc, char const *argv[]) {
  CU_pSuite pSuite = NULL;
  if (CUE_SUCCESS != CU_initialize_registry()) {
    return CU_get_error();
  }
  pSuite = CU_add_suite_with_setup_and_teardown("test_network",
                                                init_suite, clean_suite, before_test, after_test);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  if (NULL == CU_add_test(pSuite, "test_unused_port", test_unused_port)) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
