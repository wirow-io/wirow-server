

#include "gr.h"
#include "utils/html.h"

#include <CUnit/Basic.h>

#include <string.h>
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

static void test_html(void) {
  IWPOOL *pool = iwpool_create_empty();
  JBL_NODE json;
  char *ret;

  // This have been pasted into innerHTML of input and send. Json from websocket
  // "\n<!DOCTYPE html>\n"
  // "<html lang=\"e>'s'n\">\n"
  // " <script> 2; </script>\n"
  // "<sCRiPt/>"
  // "<p>"
  // "<div fff bar='zz>' onclick=\"alert(1);\" />\n"
  // "<![CDATA[ hello <sCript> </sCript> ]]>\n"
  // "<iframe> xxx </iframe>"
  // "<ul>"
  // " <li>http://greenrooms.io</li>\n"
  // " <li>greenrooms.io</li>\n"
  // "</ul>"
  // "</html>"
  // "";

  const char *raw
    = "["
        "\"]]>\\n\","
        "{\"t\":\"IFRAME\",\"c\":[\" xxx \"]},"
        "{\"t\":\"UL\",\"c\":["
          "\" \","
          "{\"t\":\"LI\",\"c\":["
            "{\"t\":\"A\",\"a\":{\"target\":\"_blank\",\"href\":\"http://greenrooms.io\"},\"c\":["
              "\"http://greenrooms.io\""
            "]}"
          "]},"
          "\"\\n \","
          "{\"t\":\"LI\",\"c\":["
            "{\"t\":\"A\",\"a\":{\"target\":\"_blank\",\"href\":\"https://greenrooms.io\"},\"c\":["
              "\"greenrooms.io\""
            "]}"
          "]},"
          "\"\\n\""
        "]}"
      "]";

  const char *processed
    = "]]&gt;\n"
      "<UL>"
        " <LI><A target=_blank href=\"http://greenrooms.io\">http://greenrooms.io</A></LI>\n"
        " <LI><A target=_blank href=\"https://greenrooms.io\">greenrooms.io</A></LI>\n"
      "</UL>";

  // Test complex html escaping
  CU_ASSERT_FALSE(jbn_from_json(raw, &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, processed);
  free(ret);

  // Test empty message
  ret = html_safe_alloc_from_jbn(0);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "");
  free(ret);

  // Test tag filtering
  CU_ASSERT_FALSE(jbn_from_json("[\"start \", {\"t\": \"scrIpT\", \"c\": [\"alert(1)\"]}, \" end\"]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "start  end");
  free(ret);

  // Test attribute filtering
  CU_ASSERT_FALSE(jbn_from_json("[{\"t\": \"IMG\", \"a\": {\"src\": \"quotes \\\" test'\", \"onsmth\": \"alert(1)\"}}]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "<IMG src=\"quotes &quot; test&#39;\"/>");
  free(ret);

  // Test link target adding
  CU_ASSERT_FALSE(jbn_from_json("[{\"t\": \"A\", \"c\": [\"link\"]}]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "<A target=_blank>link</A>");
  free(ret);

  // Test link proto filtering
  CU_ASSERT_FALSE(jbn_from_json("[{\"t\": \"A\", \"a\": {\"href\": \"https://google.com\"}, \"c\": [\"link\"]}]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "<A target=_blank href=\"https://google.com\">link</A>");
  free(ret);

  // Test link proto filtering
  CU_ASSERT_FALSE(jbn_from_json("[{\"t\": \"A\", \"a\": {\"href\": \"javascript:alert(1)\"}, \"c\": [\"link\"]}]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "<A target=_blank>link</A>");
  free(ret);

  // Test text escaping
  CU_ASSERT_FALSE(jbn_from_json("[\"fklde&jfdk<jrke 84>>>>>23734<  m,/m  9f$#W#@Ini0\"]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "fklde&amp;jfdk&lt;jrke 84&gt;&gt;&gt;&gt;&gt;23734&lt;  m,/m  9f$#W#@Ini0");
  free(ret);

  // Test attribute escaping
  CU_ASSERT_FALSE(jbn_from_json("[{\"t\": \"IMG\", \"a\": {\"src\": \"quotes \\\" test'\"}}]", &json, pool));
  ret = html_safe_alloc_from_jbn(json);
  CU_ASSERT_PTR_NOT_NULL_FATAL(ret);
  CU_ASSERT_STRING_EQUAL(ret, "<IMG src=\"quotes &quot; test&#39;\"/>");
  free(ret);

  iwpool_destroy(pool);
}

int main(int argc, char const *argv[]) {
  CU_pSuite pSuite = NULL;
  if (CUE_SUCCESS != CU_initialize_registry()) {
    return CU_get_error();
  }
  pSuite = CU_add_suite_with_setup_and_teardown("test_html",
                                                init_suite, clean_suite, before_test, after_test);
  if (NULL == pSuite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  if (NULL == CU_add_test(pSuite, "test_html", test_html)) {
    CU_cleanup_registry();
    return CU_get_error();
  }
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();
  int ret = CU_get_error() || CU_get_number_of_failures();
  CU_cleanup_registry();
  return ret;
}
