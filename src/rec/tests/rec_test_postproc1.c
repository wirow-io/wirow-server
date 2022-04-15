#include "gr.h"
#include <CUnit/Basic.h>
#include <ejdb2/iowow/iwp.h>

extern struct gr_env g_env;

const char *base = "bbc2db05-ec3e-4343-8723-b437338274b8";

static int init_suite(void) {
  iwrc rc = gr_init_noweb(3, (char*[]) {
    "rec_test_postproc1",
    "-c",
    "./rec_test_postproc1.ini",
    0
  });
  RCGO(rc, finish);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return rc != 0;
}

static int clean_suite(void) {
  gr_shutdown_noweb();
  return 0;
}

iwrc recording_postproc_test(const char *basedir, JBL room);

static JBL _room_create(void) {
  JBL jbl;
  jbl_from_json(&jbl, "{}");
  jbl_set_string(jbl, "name", "Test room");
  jbl_set_string(jbl, "uuid", base);
  jbl_set_string(jbl, "cid", base);
  jbl_set_int64(jbl, "ctime", 1617637281323LL);
  jbl_set_int64(jbl, "flags", 0);
  return jbl;
}

static void test_postproc1(void) {
  JBL room = _room_create();
  CU_ASSERT_PTR_NOT_NULL_FATAL(room);
  IWXSTR *xstr = iwxstr_new();
  iwxstr_printf(xstr, "%s/%s", g_env.recording.dir, base);
  iwrc rc = recording_postproc_test(iwxstr_ptr(xstr), room);
  CU_ASSERT_EQUAL_FATAL(rc, 0);
  jbl_destroy(&room);
  iwxstr_destroy(xstr);
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
  pSuite = CU_add_suite("rct_test1", init_suite, clean_suite);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  if ((NULL == CU_add_test(pSuite, "test_postproc1", test_postproc1))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
