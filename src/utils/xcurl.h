#include <iwnet/iwn_curl.h>
#include <ejdb2/jbl.h>

struct xcurlresp {
  IWXSTR     *body;
  JBL_NODE    json;
  const char *ctype;
  IWPOOL     *pool;
  long   response_code;
  IWLIST headers;
};

static iwrc xcurlresp_init(struct xcurlresp *resp) {
  resp->body = iwxstr_new();
  if (!resp->body) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  iwlist_init(&resp->headers, 8);
  return 0;
}

static void xcurlresp_destroy_keep(struct xcurlresp *resp) {
  if (resp) {
    iwxstr_destroy(resp->body);
    iwlist_destroy_keep(&resp->headers);
    iwpool_destroy(resp->pool);
    resp->body = 0;
    resp->json = 0;
    resp->pool = 0;
    resp->ctype = 0;
  }
}
