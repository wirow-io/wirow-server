#include "rct/rct.h"
#include "rct/rct_h264.h"
#include <CUnit/Basic.h>

static int init_suite(void) {
  return 0;
}

static int clean_suite(void) {
  return 0;
}

static void _rct_h264_test1() {

  h264_plid_t plid;
  iwrc rc = h264_plid_parse("42e01f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.level, LEVEL_3_1);

  rc = h264_plid_parse("42e00b", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.level, LEVEL_1_1);

  rc = h264_plid_parse("42f00b", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.level, LEVEL_1B);

  rc = h264_plid_parse("42C02A", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.level, LEVEL_4_2);

  // Malformed strings.
  const char *v = "";
  rc = h264_plid_parse(v, &plid);
  CU_ASSERT_TRUE(rc);
  rc = h264_plid_parse(" 42e01f", &plid);
  CU_ASSERT_TRUE(rc);
  v = "e01f";
  rc = h264_plid_parse(v, &plid);
  CU_ASSERT_TRUE(rc);

  // Invalid profile
  rc = h264_plid_parse("42e11f", &plid);
  CU_ASSERT_TRUE(rc);
  rc = h264_plid_parse("58601f", &plid);
  CU_ASSERT_TRUE(rc);
  rc = h264_plid_parse("64e01f", &plid);
  CU_ASSERT_TRUE(rc);

  // TestParsingBaseline
  rc = h264_plid_parse("42a01f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_BASELINE);

  rc = h264_plid_parse("58A01F", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_BASELINE);

  rc = h264_plid_parse("4D401f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_MAIN);

  rc = h264_plid_parse("64001f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_HIGH);

  rc = h264_plid_parse("640c1f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_CONSTRAINED_HIGH);

  rc = h264_plid_parse("42e01f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_CONSTRAINED_BASELINE);

  rc = h264_plid_parse("42C02A", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_CONSTRAINED_BASELINE);

  rc = h264_plid_parse("4de01f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_CONSTRAINED_BASELINE);

  rc = h264_plid_parse("58f01f", &plid);
  CU_ASSERT_EQUAL(rc, 0);
  CU_ASSERT_EQUAL(plid.profile, PROFILE_CONSTRAINED_BASELINE);
}

void _rct_scalbility_mode_test(void) {
  int spartial = 0;
  int temporal = 0;
  bool ksvc = false;
  rct_parse_scalability_mode("S1T2", &spartial, &temporal, &ksvc);
  CU_ASSERT_EQUAL(spartial, 1);
  CU_ASSERT_EQUAL(temporal, 2);
  CU_ASSERT_EQUAL(ksvc, false);

  rct_parse_scalability_mode("L11T2_KEY", &spartial, &temporal, &ksvc);
  CU_ASSERT_EQUAL(spartial, 11);
  CU_ASSERT_EQUAL(temporal, 2);
  CU_ASSERT_EQUAL(ksvc, true);

  rct_parse_scalability_mode("zzz", &spartial, &temporal, &ksvc);
  CU_ASSERT_EQUAL(spartial, 1);
  CU_ASSERT_EQUAL(temporal, 1);
  CU_ASSERT_EQUAL(ksvc, false);
}

int main(int argc, char const *argv[]) {
  CU_pSuite pSuite = NULL;
  if (CUE_SUCCESS != CU_initialize_registry()) {
    return CU_get_error();
  }
  pSuite = CU_add_suite("rct_test1", init_suite, clean_suite);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  if (  (NULL == CU_add_test(pSuite, "rct_h264_test1", _rct_h264_test1))
     || (NULL == CU_add_test(pSuite, "rct_scalbility_mode_test", _rct_scalbility_mode_test))) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
