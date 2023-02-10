/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include "acme.h"
#include "gr_crypt.h"
#include "lic_vars.h"
#include "utils/urandom.h"
#include "utils/xcurl.h"

#include <iwnet/iwnet.h>
#include <iwnet/bre.h>
#include <iwnet/iwn_base64.h>

#include <iowow/iwjson.h>
#include <iowow/iwpool.h>
#include <iowow/iwxstr.h>
#include <iowow/iwarr.h>
#include <iowow/iwtp.h>

#include <unistd.h>
#include <locale.h>
#include <assert.h>
#include <pthread.h>

#define RETRY_PAUSE 3

#define CHECK_JBN_TYPE(n__, label__, type__)  \
  if (!(n__) || (n__)->type != (type__)) {    \
    rc = IW_ERROR_UNEXPECTED_RESPONSE;        \
    goto label__;                             \
  }

extern struct gr_env g_env;

static char _challenge_body[1024];
static char _challenge_token[512];

struct acme {
  struct acme_context *ctx;

  private_key *akey;          // Account private key
  private_key *skey;          // Account domain private key
  br_x509_certificate *certs; // Website certs
  size_t      certs_num;      // Number of elements in certs chain
  JBL_NODE    directory;      // ACME directory
  JBL_NODE    account;        // ACME account JSON
  EJDB_DOC    acc;            // Stored in database acme info
  const char *kid;
  char       *nonce;
  uint32_t    state;

  IWPOOL  *response_pool;
  JBL_NODE reponse_json;
  IWXSTR  *response;     // Last curl response body

  IWLIST      headers;
  const char *ctype;
  long response_code;

  CURL   *curl;
  IWPOOL *pool;
};

#define _CURL_HEAD 0x01

static iwrc _curl_perform(struct acme *acme, const char *url, struct xcurlreq *data, uint32_t flags) {
  iwrc rc = 0;
  CURLcode cc = 0;
  CURL *curl = acme->curl;
  struct xcurl_cursor dcur;

  acme->response_code = 0;
  acme->reponse_json = 0;

  curl_easy_reset(curl);

  struct curl_blob cainfo_blob = {
    .data  = (void*) iwn_cacerts,
    .len   = iwn_cacerts_len,
    .flags = CURL_BLOB_NOCOPY,
  };

  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_URL, url));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &cainfo_blob));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, xcurl_body_write_xstr));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEDATA, acme->response));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, xcurl_hdr_write_iwlist));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HEADERDATA, &acme->headers));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_USERAGENT, "wirow.io"));

  if (data) {
    if (data->payload) {
      dcur.rp = data->payload;
      dcur.end = dcur.rp + data->payload_len;
      xcurlreq_hdr_add(data, "Expect", IW_LLEN("Expect"), "", 0);
      XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READFUNCTION, xcurl_read_cursor));
      XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READDATA, &dcur));
      XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_INFILESIZE, dcur.end - dcur.rp));
      XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, dcur.end - dcur.rp));
      XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_POST, 1));
    }
    if (data->headers) {
      XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HTTPHEADER, data->headers));
    }
  }

  if (flags & _CURL_HEAD) {
    XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_NOBODY, 1L));
  }

  iwpool_destroy(acme->response_pool);
  RCB(finish, acme->response_pool = iwpool_create_empty());

  for (int retry = 0; retry < 3; ++retry) {
    iwxstr_clear(acme->response);
    iwlist_destroy_keep(&acme->headers);

    cc = curl_easy_perform(curl);
    if (cc) {
      iwlog_warn("ACME | HTTP request failed: %s %s%s",
                 url,
                 curl_easy_strerror(cc),
                 retry < 2 ? " retrying in 3 sec" : "");
      if (retry < 2) {
        sleep(RETRY_PAUSE);
        continue;
      }
      XCC(cc, finish, cc);
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &acme->response_code);
    if (  (acme->response_code == 408) || (acme->response_code == 429)
       || (acme->response_code == 500) || (acme->response_code == 503) || (acme->response_code == 504)) {
      iwlog_warn("ACME | HTTP request failed: %s %ld%s", url, acme->response_code,
                 retry < 2 ? " retrying in 5 sec" : "");
      if (retry < 2) {
        sleep(RETRY_PAUSE);
        continue;
      }
      rc = IW_ERROR_FAIL;
      goto finish;
    }

    break;
  }

  const char *hv = xcurl_hdr_find("replay-nonce", &acme->headers);
  if (hv) {
    free(acme->nonce);
    RCB(finish, acme->nonce = strdup(hv));
  }
  hv = xcurl_hdr_find("content-type", &acme->headers);
  if (hv && (strstr(hv, "json") || strstr(hv, "JSON"))) {
    RCC(rc, finish, jbn_from_json(iwxstr_ptr(acme->response), &acme->reponse_json, acme->response_pool));
  }
  acme->ctype = hv;

finish:
  return rc;
}

static iwrc _jwk_fill(const br_ec_private_key *skey, JBL jwk) {
  iwrc rc = 0;
  size_t xlen, len;
  br_ec_public_key pk;
  unsigned pkbuf[BR_EC_KBUF_PUB_MAX_SIZE];

  if (!br_ec_compute_pub(br_ec_get_default(), &pk, pkbuf, skey)) {
    rc = GR_ERROR_CRYPTO_API_FAIL;
    goto finish;
  }

  switch (skey->curve) {
    case BR_EC_secp256k1:
    case BR_EC_secp256r1:
      xlen = 32;
      RCC(rc, finish, jbl_set_string(jwk, "crv", "P-256"));
      break;
    case BR_EC_secp384r1:
      xlen = 48;
      RCC(rc, finish, jbl_set_string(jwk, "crv", "P-384"));
      break;
    case BR_EC_secp521r1:
      xlen = 66;
      RCC(rc, finish, jbl_set_string(jwk, "crv", "P-521"));
      break;
    default:
      rc = IW_ERROR_INVALID_STATE;
      iwlog_ecode_error2(rc, "ACME | Unsupported EC curve");
      goto finish;
  }

  RCC(rc, finish, jbl_set_string(jwk, "kty", "EC"));

  char x[96], y[96];
  if (!iwn_base64_encode(x, sizeof(x), &len, pk.q + 1, xlen, base64_VARIANT_URLSAFE_NO_PADDING)) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }
  if (!iwn_base64_encode(y, sizeof(y), &len, pk.q + 1 + xlen, xlen, base64_VARIANT_URLSAFE_NO_PADDING)) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }

  RCC(rc, finish, jbl_set_string(jwk, "x", x));
  RCC(rc, finish, jbl_set_string(jwk, "y", y));

finish:
  return rc;
}

static iwrc _jwk_thumbprint(struct acme *acme, char **out, size_t *out_len) {
  *out = 0;
  *out_len = 0;

  iwrc rc = 0;
  if (!acme->akey) {
    iwlog_error2("ACME | No account key");
    return IW_ERROR_INVALID_STATE;
  }

  IWXSTR *xstr = 0;
  JBL jwk = 0;
  const br_ec_private_key *skey = &acme->akey->key.ec;

  br_hash_compat_context hcc;
  unsigned char hash_data[32];
  hcc.vtable = &br_sha256_vtable;

  RCC(rc, finish, jbl_create_empty_object(&jwk));
  RCC(rc, finish, _jwk_fill(skey, jwk));

  RCB(finish, xstr = iwxstr_new());
  RCC(rc, finish, jbl_as_json(jwk, jbl_xstr_json_printer, xstr, 0));

  hcc.vtable = &br_sha256_vtable;
  hcc.vtable->init(&hcc.vtable);
  hcc.vtable->update(&hcc.vtable, iwxstr_ptr(xstr), iwxstr_size(xstr));
  hcc.vtable->out(&hcc.vtable, hash_data);

  *out = iwn_base64_encode_url(hash_data, sizeof(hash_data), out_len);
  if (*out == 0) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }

finish:
  jbl_destroy(&jwk);
  iwxstr_destroy(xstr);
  return rc;
}

static iwrc _jws(struct acme *acme, const char *url, JBL payload, char **out) {
  *out = 0;

  size_t len;
  const br_hash_class *hc;

  iwrc rc = 0;
  char *buf = 0;
  JBL protected = 0, jbl = 0;
  IWXSTR *xstr = 0, *xstr2 = 0;

  if (!acme->akey) {
    iwlog_error2("ACME | No account key");
    return IW_ERROR_INVALID_STATE;
  }

  const br_ec_private_key *skey = &acme->akey->key.ec;

  if (!acme->nonce) {
    iwlog_error2("ACME | No nonce");
    return IW_ERROR_INVALID_STATE;
  }

  RCB(finish, xstr = iwxstr_new());
  RCB(finish, xstr2 = iwxstr_new());

  RCC(rc, finish, jbl_create_empty_object(&protected));
  switch (skey->curve) {
    // ES256        | ECDSA using P-256 and SHA-256 | Recommended+
    // ES384        | ECDSA using P-384 and SHA-384 | Optional
    // ES512        | ECDSA using P-521 and SHA-512 | Optional
    case BR_EC_secp256k1:
    case BR_EC_secp256r1:
      hc = &br_sha256_vtable;
      RCC(rc, finish, jbl_set_string(protected, "alg", "ES256"));
      break;
    case BR_EC_secp384r1:
      hc = &br_sha384_vtable;
      RCC(rc, finish, jbl_set_string(protected, "alg", "ES384"));
      break;
    case BR_EC_secp521r1:
      hc = &br_sha512_vtable;
      RCC(rc, finish, jbl_set_string(protected, "alg", "ES512"));
      break;
    default:
      rc = IW_ERROR_INVALID_STATE;
      iwlog_ecode_error2(rc, "ACME | Unsupported EC curve");
      goto finish;
  }
  if (acme->kid) {
    RCC(rc, finish, jbl_set_string(protected, "kid", acme->kid));
  } else {
    RCC(rc, finish, jbl_create_empty_object(&jbl));
    RCC(rc, finish, _jwk_fill(skey, jbl));
    RCC(rc, finish, jbl_set_nested(protected, "jwk", jbl));
    jbl_destroy(&jbl);
  }
  RCC(rc, finish, jbl_set_string(protected, "nonce", acme->nonce));
  RCC(rc, finish, jbl_set_string(protected, "url", url));

  RCC(rc, finish, jbl_create_empty_object(&jbl));

  // Pack <base64 protected>.<base64 payload> into xstr
  iwxstr_clear(xstr2);
  RCC(rc, finish, jbl_as_json(protected, jbl_xstr_json_printer, xstr2, 0));
  buf = iwn_base64_encode_url(iwxstr_ptr(xstr2), iwxstr_size(xstr2), &len);
  if (!buf) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }
  RCC(rc, finish, jbl_set_string(jbl, "protected", buf));
  RCC(rc, finish, iwxstr_cat(xstr, buf, len - 1 /* Strip trailing zero */));
  free(buf), buf = 0;

  RCC(rc, finish, iwxstr_cat(xstr, ".", 1));
  iwxstr_clear(xstr2);
  if (payload) {
    RCC(rc, finish, jbl_as_json(payload, jbl_xstr_json_printer, xstr2, 0));
  }
  buf = iwn_base64_encode_url(iwxstr_ptr(xstr2), iwxstr_size(xstr2), &len);
  if (!buf) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }
  RCC(rc, finish, jbl_set_string(jbl, "payload", buf));
  RCC(rc, finish, iwxstr_cat(xstr, buf, len - 1 /* Strip trailing zero */));
  free(buf), buf = 0;

  // Compute signature
  {
    unsigned char sig[257];
    br_hash_compat_context hcc;
    hcc.vtable = hc;
    size_t hash_len = (hcc.vtable->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;
    unsigned char hash_data[hash_len];

    hcc.vtable->init(&hcc.vtable);
    hcc.vtable->update(&hcc.vtable, iwxstr_ptr(xstr), iwxstr_size(xstr));
    hcc.vtable->out(&hcc.vtable, hash_data);

    br_ecdsa_sign s = br_ecdsa_sign_raw_get_default();
    size_t sig_len = s(br_ec_get_default(), hc, hash_data, skey, sig);
    if (sig_len == 0) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | Failed to create EC signature");
      goto finish;
    }
    buf = iwn_base64_encode_url(sig, sig_len, &len);
    if (!buf) {
      rc = IW_ERROR_FAIL;
      goto finish;
    }
    RCC(rc, finish, jbl_set_string(jbl, "signature", buf));
  }

  iwxstr_clear(xstr);
  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));
  *out = iwxstr_destroy_keep_ptr(xstr), xstr = 0;

finish:
  free(buf);
  jbl_destroy(&protected);
  jbl_destroy(&jbl);
  iwxstr_destroy(xstr);
  iwxstr_destroy(xstr2);
  return rc;
}

static iwrc _acme_post(struct acme *acme, const char *url, JBL payload) {
  if (!acme->nonce) {
    iwlog_error2("ACME | No nonce");
    return IW_ERROR_INVALID_STATE;
  }
  char *jws;
  iwrc rc = RCR(_jws(acme, url, payload, &jws));
  struct xcurlreq d = {
    .payload     = jws,
    .payload_len = strlen(jws),
    .headers     = curl_slist_append(0, "Content-Type: application/jose+json")
  };
  rc = _curl_perform(acme, url, &d, 0);
  curl_slist_free_all(d.headers);
  free(jws);
  return rc;
}

static iwrc _acme_post_jbn(struct acme *acme, const char *url, JBL_NODE payload) {
  JBL jbl = 0;
  iwrc rc = jbl_from_node(&jbl, payload);
  if (!rc) {
    rc = _acme_post(acme, url, jbl);
  }
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _acme_post_format(struct acme *acme, const char *url, const char *format, ...) {
  JBL jbl;
  va_list va;

  va_start(va, format);
  iwrc rc = jbl_from_json_printf_va(&jbl, format, va);
  va_end(va);
  RCGO(rc, finish);

  RCC(rc, finish, _acme_post(acme, url, jbl));

finish:
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _skey_create(private_key **skeyp, char **pemp) {
  if (pemp) {
    *pemp = 0;
  }
  uint8_t seed[20];
  br_hmac_drbg_context rng;
  char *derbuf = 0, *pembuf = 0;

  iwrc rc = RCR(urandom(seed, sizeof(seed)));
  private_key *skey = *skeyp = malloc(sizeof(**skeyp));
  RCA(skey, finish);

  void *skbuf = malloc(BR_EC_KBUF_PRIV_MAX_SIZE);
  RCA(skbuf, finish);

  br_hmac_drbg_init(&rng, &br_sha256_vtable, seed, sizeof(seed));
  const br_ec_impl *iec = br_ec_get_default();

  if (!br_ec_keygen(&rng.vtable, iec, &skey->key.ec, skbuf, BR_EC_secp256r1)) {
    rc = GR_ERROR_CRYPTO_API_FAIL;
    goto finish;
  }
  skey->key_type = BR_KEYTYPE_EC;

  if (pemp) {
    size_t len = br_encode_ec_raw_der(0, &skey->key.ec, 0), len2;
    if (len == 0) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | Failed to encode EC private key as DER");
      goto finish;
    }
    RCB(finish, derbuf = malloc(len + 1));
    if (len != br_encode_ec_raw_der(derbuf, &skey->key.ec, 0)) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | Failed to encode EC private key as DER");
      goto finish;
    }
    len2 = br_pem_encode(0, derbuf, len, BR_ENCODE_PEM_EC_RAW, 0);
    if (len2 == 0) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | Failed to encode EC private key as PEM");
      goto finish;
    }
    RCB(finish, pembuf = malloc(len2 + 1));
    if (len2 != br_pem_encode(pembuf, derbuf, len, BR_ENCODE_PEM_EC_RAW, 0)) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | Failed to encode EC private key as PEM");
      goto finish;
    }
    *pemp = pembuf;
  }

finish:
  free(derbuf);
  if (rc) {
    free(pembuf);
    free_private_key(skey);
    *skeyp = 0;
    if (pemp) {
      *pemp = 0;
    }
  }
  return rc;
}

/// Load database account.
static iwrc _account_load(struct acme *acme) {
// acme: ACME protocol certs / state
//   - domain    {string}   Domain name
//   - endpoint  {string}   Acme endpoint URL
//   - ets       {int64}    Certificate expire time
//   - cts       {int64}    Certificate creation time
//   - kid       {string?}  Account URL
//   - certs     {string?}  PEM encoded certs chain
//   - skey      {string?}  PEM encoded site private key
//   - akey      {string?}  PEM encoded account key

  if (acme->acc) {
    return 0;
  }

  iwrc rc = 0;
  JQL q = 0;
  JBL jbl = 0;
  EJDB db = g_env.db;

  JBL_NODE n;
  EJDB_DOC doc;

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "domain", g_env.domain_name));
  RCC(rc, finish, jbl_set_string(jbl, "endpoint", g_env.acme.endpoint));

  RCC(rc, finish, jql_create(&q, "acme", "/[domain = :?] and /[endpoint = :?] | upsert :?"));
  RCC(rc, finish, jql_set_str(q, 0, 0, g_env.domain_name));
  RCC(rc, finish, jql_set_str(q, 0, 1, g_env.acme.endpoint));
  RCC(rc, finish, jql_set_json_jbl(q, 0, 2, jbl));
  RCC(rc, finish, ejdb_list(db, q, &doc, 1, acme->pool));

  acme->acc = doc;

  uint64_t time;
  int64_t ets = 0, cts = 0;

  if (!doc->node) {
    RCC(rc, finish, jbl_to_node(doc->raw, &doc->node, true, acme->pool));
  }

  rc = jbn_at(doc->node, "/ets", &n);
  if (!rc && (n->type == JBV_I64)) {
    ets = n->vi64;
  }

  rc = jbn_at(doc->node, "/cts", &n);
  if (!rc && (n->type == JBV_I64)) {
    cts = n->vi64;
  }

  RCC(rc, finish, iwp_current_time_ms(&time, false));
  time /= 1000;

  rc = jbn_at(doc->node, "/kid", &n); // Check

  if ((rc != 0) || (n->type != JBV_STR)) {
    acme->state |= ACME_CREATE_ACC;
  }
  if (time >= ets - (ets - cts) / 3) {
    acme->state |= ACME_RENEW;
  }
  rc = 0;

  if (acme->state & ACME_CREATE_ACC) {
    // Skip loading of account data
    goto finish;
  }

  // Save account url
  acme->kid = n->vptr;

  // Account private key PEM
  rc = jbn_at(doc->node, "/akey", &n);
  if (rc || (n->type != JBV_STR)) {
    rc = IW_ERROR_INVALID_VALUE;
    goto finish;
  }
  acme->akey = read_private_key_data((void*) n->vptr, n->vsize);
  if (!acme->akey) {
    rc = GR_ERROR_CRYPTO_API_FAIL;
    goto finish;
  }

  // Domain private key PEM
  rc = jbn_at(doc->node, "/skey", &n);
  if (!rc && (n->type == JBV_STR)) {
    acme->skey = read_private_key_data((void*) n->vptr, n->vsize);
    if (!acme->skey) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      goto finish;
    }
  }

  // Domain certs
  rc = jbn_at(doc->node, "/certs", &n);
  if (!rc && (n->type == JBV_STR)) {
    acme->certs = read_certificates_data((void*) n->vptr, n->vsize, &acme->certs_num);
    if (!acme->certs) {
      rc = GR_ERROR_CRYPTO_API_FAIL;
      goto finish;
    }
  }

  rc = 0;

finish:
  jql_destroy(&q);
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _directory_load(struct acme *acme) {
  if (acme->directory) {
    return 0;
  }
  JBL_NODE n;
  const char *url = g_env.acme.endpoint;
  iwrc rc = _curl_perform(acme, url, 0, 0);
  RCGO(rc, finish);
  if (acme->response_code != 200) {
    rc = GR_ERROR_ACME_API_FAIL;
    iwlog_ecode_error3(rc);
    goto finish;
  }

  RCC(rc, finish, jbn_from_json(iwxstr_ptr(acme->response), &acme->directory, acme->pool));

  rc = jbn_at(acme->directory, "/newNonce", &n);
  if (rc || (n->type != JBV_STR)) {
    rc = GR_ERROR_ACME_API_FAIL;
    iwlog_ecode_error3(rc);
    goto finish;
  }

  RCC(rc, finish, _curl_perform(acme, n->vptr, 0, _CURL_HEAD));
  if (acme->response_code != 200) {
    rc = GR_ERROR_ACME_API_FAIL;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  if (!acme->nonce) {
    iwlog_error2("ACME | No nonce");
    rc = IW_ERROR_INVALID_STATE;
  }

finish:
  return rc;
}

static iwrc _directory_endpoint(struct acme *acme, const char *path, const char **out) {
  if (!acme->directory) {
    return IW_ERROR_INVALID_STATE;
  }
  JBL_NODE n;
  iwrc rc = RCR(jbn_at(acme->directory, path, &n));
  if (n->type != JBV_STR) {
    return IW_ERROR_INVALID_VALUE;
  }
  *out = n->vptr;
  return rc;
}

static iwrc _acme_check_error(struct acme *acme, const char *url, int expected_code, const char *tag) {
  if (!acme->reponse_json || !acme->ctype || !strstr(acme->ctype, "application/problem+json")) {
    if (acme->kid == 0) {
      iwlog_error("ACME | Failed to get accout kid, url: %s", url);
      return GR_ERROR_ACME_API_FAIL;
    }
    if ((expected_code > 0) && (acme->response_code != expected_code)) {
      iwlog_error("ACME | Unexpected response code: %ld, url: %s", acme->response_code, url);
      return GR_ERROR_ACME_API_FAIL;
    }
    return 0;
  }
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  jbn_as_json(acme->reponse_json, jbl_xstr_json_printer, xstr, 0);
  iwlog_error("ACME | Response error[%s] url: %s message: %s", tag, url, iwxstr_ptr(xstr));
  iwxstr_destroy(xstr);
  return GR_ERROR_ACME_API_FAIL;
}

static iwrc _account_create(struct acme *acme, const char *url) {
  JBL jbl = 0, jbl2 = 0;
  assert(g_env.domain_name);

  iwrc rc = RCR(jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_bool(jbl, "termsOfServiceAgreed", true));
  RCC(rc, finish, jbl_create_empty_array(&jbl2));
  #ifdef LICENSE_EMAIL
  RCC(rc, finish, jbl_set_string(jbl2, 0, "mailto:"  LICENSE_EMAIL));
  #else
  RCC(rc, finish, jbl_set_string_printf(jbl2, 0, "mailto:%s@wirow.io", g_env.domain_name));
  #endif
  RCC(rc, finish, jbl_set_nested(jbl, "contact", jbl2));

  RCC(rc, finish, _acme_post(acme, url, jbl));

  acme->kid = xcurl_hdr_find("location", &acme->headers);
  if (acme->kid == 0) {
    iwlog_error2("ACME | Failed to get accout kid");
    rc = GR_ERROR_ACME_API_FAIL;
  }
  acme->kid = iwpool_strdup(acme->pool, acme->kid, &rc);

finish:
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  return rc;
}

static iwrc _acme_ensure_key(struct acme *acme, const char *keyfield) {
  iwrc rc = 0;
  JBL jbl = 0;
  char *pem = 0;
  private_key **keyp;

  if (!acme->acc) {
    rc = IW_ERROR_INVALID_STATE;
    iwlog_ecode_error2(rc, "ACME | No account document");
    return rc;
  }
  if (strcmp("akey", keyfield) == 0) {
    if (acme->akey) {
      return 0;
    } else {
      keyp = &acme->akey;
    }
  } else if (strcmp("skey", keyfield) == 0) {
    if (acme->skey) {
      return 0;
    } else {
      keyp = &acme->skey;
    }
  } else {
    rc = IW_ERROR_INVALID_ARGS;
    return rc;
  }

  RCC(rc, finish, _skey_create(keyp, &pem));
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, keyfield, pem));
  rc = ejdb_patch_jbl(g_env.db, "acme", jbl, acme->acc->id);

finish:
  jbl_destroy(&jbl);
  free(pem);
  return rc;
}

static iwrc _account_ensure(struct acme *acme) {
  JBL_NODE n;
  const char *url;
  JBL jbl = 0;

  iwrc rc = RCR(_directory_endpoint(acme, "/newAccount", &url));
  RCC(rc, finish, _acme_ensure_key(acme, "akey"));
  if (acme->kid) {
    goto finish;
  }
  RCC(rc, finish, _acme_post_format(acme, url, "{\"onlyReturnExisting\":true}"));
  switch (acme->response_code) {
    case 200:
      acme->kid = xcurl_hdr_find("location", &acme->headers);
      if (acme->kid == 0) {
        iwlog_error2("ACME | Failed to get accout kid");
        rc = GR_ERROR_ACME_API_FAIL;
        break;
      }
      acme->kid = iwpool_strdup(acme->pool, acme->kid, &rc);
      break;
    case 400:
      RCC(rc, finish, _account_create(acme, url));
      RCC(rc, finish, _acme_check_error(acme, url, 0, __func__));
      break;
    default:
      rc = GR_ERROR_ACME_API_FAIL;
      break;
  }
  RCGO(rc, finish);
  RCC(rc, finish, jbn_at(acme->reponse_json, "/status", &n));
  if ((n->type == JBV_STR) && (strcmp(n->vptr, "valid") != 0)) {
    rc = GR_ERROR_ACME_API_FAIL;
    iwlog_ecode_error2(rc, "ACME | Account is not valid");
  }
  RCC(rc, finish, jbn_clone(acme->reponse_json, &acme->account, acme->pool));
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "kid", acme->kid));
  RCC(rc, finish, ejdb_patch_jbl(g_env.db, "acme", jbl, acme->acc->id));

finish:
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _authorize(struct acme *acme, JBL_NODE authorizations, IWPOOL *pool) {
  iwrc rc = 0;
  size_t len;
  char *thumbprint = 0;
  const char *url, *status, *token, *challenge_url;

  JBL_NODE n = authorizations->child, auth, challenge = 0;
  if (!n || (n->type != JBV_STR) || n->next) {
    return IW_ERROR_UNEXPECTED_RESPONSE;
  }

  url = n->vptr;
  RCC(rc, finish, _acme_post(acme, url, 0));
  RCC(rc, finish, _acme_check_error(acme, url, 200, __func__));

  RCC(rc, finish, jbn_clone(acme->reponse_json, &auth, pool));
  RCC(rc, finish, jbn_at(auth, "/status", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);

  status = n->vptr;
  if (strcmp("valid", status) == 0) {
    goto finish;
  }
  if (strcmp("invalid", status) == 0) {
    rc = GR_ERROR_ACME_CHALLENGE_FAIL;
    goto finish;
  }
  RCC(rc, finish, jbn_at(auth, "/challenges", &n));
  CHECK_JBN_TYPE(n, finish, JBV_ARRAY);

  for (n = n->child; n; n = n->next) {
    JBL_NODE n2;
    rc = jbn_at(n, "/type", &n2);
    if (!rc && (n2->type == JBV_STR) && (strcmp("http-01", n2->vptr) == 0)) {
      challenge = n;
      break;
    }
  }
  if (!challenge) {
    rc = IW_ERROR_UNEXPECTED_RESPONSE;
    iwlog_ecode_error(rc, "ACME | Unable to find http-01 challenge for: %s endpoint: %s",
                      g_env.domain_name, g_env.acme.endpoint);
    goto finish;
  }

  RCC(rc, finish, jbn_at(challenge, "/url", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  challenge_url = n->vptr;

  RCC(rc, finish, jbn_at(challenge, "/token", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  token = n->vptr;

  RCC(rc, finish, _jwk_thumbprint(acme, &thumbprint, &len));
  if ((n->vsize + len + 2 > sizeof(_challenge_body)) || (n->vsize + 1 > sizeof(_challenge_token))) {
    rc = IW_ERROR_UNEXPECTED_RESPONSE;
    goto finish;
  }

  memcpy(_challenge_token, token, n->vsize);
  _challenge_token[n->vsize] = '\0';
  memcpy(_challenge_body, token, n->vsize);
  _challenge_body[n->vsize] = '.';
  memcpy(_challenge_body + n->vsize + 1, thumbprint, len);
  _challenge_body[n->vsize + len + 1] = '\0';

  // Activate challenge check
  iwlog_info("ACME | Starting http-01 challenge");
  RCC(rc, finish, _acme_post_format(acme, challenge_url, "{}"));
  RCC(rc, finish, _acme_check_error(acme, challenge_url, 200, __func__));

  while (1) {
    RCC(rc, finish, _acme_post(acme, challenge_url, 0));
    RCC(rc, finish, _acme_check_error(acme, challenge_url, 200, __func__));
    RCC(rc, finish, jbn_at(acme->reponse_json, "/status", &n));
    CHECK_JBN_TYPE(n, finish, JBV_STR);
    status = n->vptr;
    if (strcmp(status, "valid") == 0) {
      break;
    }
    if ((strcmp(status, "pending") != 0) && (strcmp(status, "processing") != 0)) {
      rc = GR_ERROR_ACME_CHALLENGE_FAIL;
      iwlog_ecode_error2(rc, "ACME | Failed complete http-01 challenge");
      goto finish;
    }
    iwlog_info("ACME | Challenge status: %s", status);
    sleep(RETRY_PAUSE);
  }

  iwlog_info("ACME | Challenge completed successfully");

finish:
  free(thumbprint);
  memset(_challenge_token, 0, sizeof(_challenge_token));
  memset(_challenge_body, 0, sizeof(_challenge_body));
  return rc;
}

static iwrc _finalize(struct acme *acme, const char *url) {
  #ifdef LICENSE_EMAIL
  const char *email = LICENSE_EMAIL;
  #else
  char buf[255];
  const char *email = buf;
  snprintf(buf, sizeof(buf), "%s@wirow.io", g_env.domain_name);
  #endif

  size_t len, csrlen;
  JBL jbl = 0;
  uint8_t *derbuf = 0;
  char *csrbuf = 0;
  const char *error = 0;

  iwrc rc = RCR(_acme_ensure_key(acme, "skey"));
  bre_ec_csr csr = {
    .subj         = {
      .cn         = g_env.domain_name,
      .dns_name   = g_env.domain_name,
      .email      = email
    },
    .sk           = acme->skey->key.ec,
    .signature_hc = &br_sha256_vtable
  };

  derbuf = bre_csr_ec_der_create(malloc, free, &csr, &len, &error);
  if (!derbuf) {
    rc = GR_ERROR_CRYPTO_API_FAIL;
    iwlog_ecode_error(rc, "ACME | %s", error ? error : "Failed to create CSR");
    goto finish;
  }
  csrbuf = iwn_base64_encode_url(derbuf, len, &csrlen);
  if (!csrbuf) {
    rc = IW_ERROR_FAIL;
    iwlog_ecode_error3(rc);
    goto finish;
  }

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "csr", csrbuf));
  RCC(rc, finish, _acme_post(acme, url, jbl));
  RCC(rc, finish, _acme_check_error(acme, url, 200, __func__));

finish:
  jbl_destroy(&jbl);
  free(derbuf);
  free(csrbuf);
  return rc;
}

static iwrc _certificate(struct acme *acme, const char *url) {
  iwrc rc = 0;
  JBL jbl = 0;
  int64_t ets = 0, cts = 0;

  if (g_env.log.verbose) {
    iwlog_info("ACME | Retrieving certificates...");
  }

  RCC(rc, finish, _acme_post(acme, url, 0));
  RCC(rc, finish, _acme_check_error(acme, url, 200, __func__));

  if (g_env.log.verbose) {
    iwlog_info("ACME | Certificates chain for %s\n%s", g_env.domain_name, iwxstr_ptr(acme->response));
  }
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "certs", iwxstr_ptr(acme->response)));

  if (acme->certs_num) {
    free_certificates(acme->certs, acme->certs_num);
    acme->certs_num = 0;
  }
  acme->certs = read_certificates_data(iwxstr_ptr(acme->response), iwxstr_size(acme->response), &acme->certs_num);
  if (!acme->certs) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    iwlog_ecode_error2(rc, "ACME | Failed to read certificates chain");
    goto finish;
  }
  for (int i = 0; i < acme->certs_num; ++i) {
    br_x509_decoder_context dctx;
    br_x509_decoder_init(&dctx, 0, 0);
    br_x509_decoder_push(&dctx, acme->certs[i].data, acme->certs[i].data_len);
    if (!ets && dctx.decoded && !dctx.isCA && dctx.notbefore_days && dctx.notafter_days) {
      cts = ((int64_t) dctx.notbefore_days - 719528) * 86400 + dctx.notbefore_seconds;
      ets = ((int64_t) dctx.notafter_days - 719528) * 86400 + dctx.notafter_seconds;
      break;
    }
  }
  if (!cts || !ets) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    iwlog_ecode_error2(rc, "ACME | Failed to read certificate dates");
    goto finish;
  }
  RCC(rc, finish, jbl_set_int64(jbl, "cts", cts));
  RCC(rc, finish, jbl_set_int64(jbl, "ets", ets));
  RCC(rc, finish, ejdb_patch_jbl(g_env.db, "acme", jbl, acme->acc->id));

finish:
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _order(struct acme *acme) {
  const char *url, *order_url, *finalize_url, *status, *ptr;
  JBL_NODE payload, order, authorizations, n, n2;

  iwrc rc = RCR(_directory_endpoint(acme, "/newOrder", &url));
  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCC(rc, finish, _acme_ensure_key(acme, "skey"));

  RCC(rc, finish, jbn_from_json("{}", &payload, pool));
  RCC(rc, finish, jbn_add_item_arr(payload, "identifiers", &n, pool));
  RCC(rc, finish, jbn_add_item_obj(n, 0, &n2, pool));
  RCC(rc, finish, jbn_add_item_str(n2, "type", "dns", 3, 0, pool));
  RCC(rc, finish, jbn_add_item_str(n2, "value", g_env.domain_name, -1, 0, pool));

  RCC(rc, finish, _acme_post_jbn(acme, url, payload));
  RCC(rc, finish, _acme_check_error(acme, url, 201, __func__));

  ptr = xcurl_hdr_find("location", &acme->headers);
  if (!ptr) {
    rc = GR_ERROR_ACME_API_FAIL;
    iwlog_ecode_error3(rc);
    goto finish;
  }
  order_url = iwpool_strdup(pool, ptr, &rc);
  RCGO(rc, finish);

  RCC(rc, finish, jbn_clone(acme->reponse_json, &order, pool));
  RCC(rc, finish, jbn_at(order, "/finalize", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  finalize_url = n->vptr;

  RCC(rc, finish, jbn_at(order, "/status", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  status = n->vptr;

  RCC(rc, finish, jbn_at(order, "/authorizations", &authorizations));
  CHECK_JBN_TYPE(authorizations, finish, JBV_ARRAY);

  if (strcmp(status, "ready") == 0) {
    goto finalize;
  }

  RCC(rc, finish, _authorize(acme, authorizations, pool));

  while (1) {
    RCC(rc, finish, _acme_post(acme, order_url, 0));
    RCC(rc, finish, _acme_check_error(acme, order_url, 200, __func__));
    RCC(rc, finish, jbn_at(acme->reponse_json, "/status", &n));
    CHECK_JBN_TYPE(n, finish, JBV_STR);
    status = n->vptr;
    if (strcmp(status, "ready") == 0) {
      break;
    }
    if (strcmp(status, "valid") == 0) {
      goto valid;
    }
    if (strcmp(status, "invalid") == 0) {
      rc = GR_ERROR_ACME_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | order status is invalid");
      goto finish;
    }
    sleep(RETRY_PAUSE);
  }

finalize:
  RCC(rc, finish, _finalize(acme, finalize_url));
  while (1) {
    // Get order status from _finalize() response
    RCC(rc, finish, jbn_at(acme->reponse_json, "/status", &n));
    CHECK_JBN_TYPE(n, finish, JBV_STR);
    status = n->vptr;
    if (strcmp(status, "valid") == 0) {
      goto valid;
    }
    if (strcmp(status, "invalid") == 0) {
      rc = GR_ERROR_ACME_API_FAIL;
      iwlog_ecode_error2(rc, "ACME | order status is invalid");
      goto finish;
    }
    sleep(RETRY_PAUSE);
    RCC(rc, finish, _acme_post(acme, order_url, 0));
    RCC(rc, finish, _acme_check_error(acme, order_url, 200, __func__));
  }

valid:
  RCC(rc, finish, jbn_at(acme->reponse_json, "/certificate", &n));
  CHECK_JBN_TYPE(n, finish, JBV_STR);
  RCC(rc, finish, _certificate(acme, n->vptr));

finish:
  iwpool_destroy(pool);
  return rc;
}

static bool _sync_pending = false;

iwrc acme_sync(uint32_t flags, struct acme_context *out_ctx) {
  if (!g_env.acme.endpoint) {
    iwlog_error2("ACME | Protocol endpoint was not set");
    return IW_ERROR_INVALID_STATE;
  }
  if (!__sync_bool_compare_and_swap(&_sync_pending, false, true)) {
    return IW_ERROR_AGAIN;
  }
  iwrc rc = 0;

  memset(out_ctx, 0, sizeof(*out_ctx));
  struct acme acme = {
    .ctx = out_ctx,
  };

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);
  acme.pool = pool;

  RCB(finish, acme.response = iwxstr_new());
  acme.curl = curl_easy_init();
  if (!acme.curl) {
    rc = GR_ERROR_CURL_API_FAIL;
    goto finish;
  }

  RCC(rc, finish, _account_load(&acme));
  acme.state |= flags;

  if (!(acme.state & (ACME_CREATE_ACC | ACME_RENEW))) {
    goto finish;
  }

  iwlog_info("ACME | Domain: %s API: %s", g_env.domain_name, g_env.acme.endpoint);

  RCC(rc, finish, _directory_load(&acme));
  RCC(rc, finish, _account_ensure(&acme));
  if (acme.state & ACME_RENEW) {
    RCC(rc, finish, _order(&acme));
  }

  // Save results and transfer the ownership to caller
  out_ctx->pk = acme.skey, acme.skey = 0;
  out_ctx->certs = acme.certs, acme.certs = 0;
  out_ctx->certs_num = acme.certs_num, acme.certs_num = 0;

finish:
  curl_easy_cleanup(acme.curl);
  iwxstr_destroy(acme.response);
  iwpool_destroy(acme.response_pool);
  iwpool_destroy(acme.pool);
  free_private_key(acme.akey);
  free_private_key(acme.skey);
  free_certificates(acme.certs, acme.certs_num);
  free(acme.nonce);
  iwlist_destroy_keep(&acme.headers);

  __sync_bool_compare_and_swap(&_sync_pending, true, false);
  return rc;
}

void acme_context_clean(struct acme_context *ctx) {
  if (!ctx) {
    return;
  }
  if (ctx->certs_num) {
    free_certificates(ctx->certs, ctx->certs_num);
    ctx->certs_num = 0;
  }
  if (ctx->pk) {
    free_private_key(ctx->pk);
    ctx->pk = 0;
  }
}

struct acme_sync {
  const char *domain_name;
  bool     (*consumer)(struct acme_context*);
  uint32_t flags;
};

static void _acme_sync_detached(void *op) {
  struct acme_sync *s = op;
  struct acme_context ctx = { 0 };

  iwrc rc = acme_sync(s->flags, &ctx);
  if (rc) {
    if (rc == IW_ERROR_AGAIN) {
      rc = 0;
    }
    goto finish;
  }

  if (s->consumer && s->consumer(&ctx)) {
    memset(&ctx, 0, sizeof(ctx));
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    if (s->consumer) {
      s->consumer(0);
    }
  }
  acme_context_clean(&ctx);
  free(s);
}

iwrc acme_sync_consumer(uint32_t flags, bool (*consumer)(struct acme_context*)) {
  struct acme_context ctx = { 0 };
  iwrc rc = acme_sync(flags, &ctx);
  RCGO(rc, finish);
  if (consumer && consumer(&ctx)) {
    memset(&ctx, 0, sizeof(ctx));
  }
finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    if (consumer) {
      consumer(0);
    }
  }
  acme_context_clean(&ctx);
  return rc;
}

iwrc acme_sync_consumer_detached(uint32_t flags, bool (*consumer)(struct acme_context*)) {
  struct acme_sync *s = malloc(sizeof(*s));
  if (!s) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  *s = (struct acme_sync) {
    .consumer = consumer,
    .flags = flags
  };
  iwrc rc = iwtp_schedule(g_env.tp, _acme_sync_detached, s);
  if (rc) {
    free(s);
    iwlog_ecode_error3(rc);
  }
  return rc;
}

bool acme_consumer_callback(struct acme_context *ctx) {
  static bool first_run = true;

  if (!ctx) {
    if (first_run) {
      // Error during the first run
      gr_shutdown();
    }
    return false;
  }

  if (!first_run && (ctx->certs_num == 0)) {
    // ACME certs are up-to date
    return false;
  }

  first_run = false;

  iwlog_info2("ACME | Applying certificates");
  if (!g_env.domain_name || !g_env.acme.endpoint) {
    iwlog_error2("ACME | HTTPS listen point not found, inconsistent state, shutting down");
    gr_shutdown();
    return false;
  }

  iwrc rc = 0;
  JQL q = 0;
  JBL_NODE n;
  EJDB_DOC doc;
  const char *private_key, *certs;
  ssize_t private_key_len, certs_len;

  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);

  RCC(rc, finish, jql_create(&q, "acme", "/[domain = :?] and /[endpoint = :?]"));
  RCC(rc, finish, jql_set_str(q, 0, 0, g_env.domain_name));
  RCC(rc, finish, jql_set_str(q, 0, 1, g_env.acme.endpoint));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc, 1, pool));

  if (!doc) {
    rc = IW_ERROR_INVALID_STATE;
    iwlog_ecode_error2(rc, "ACME | No ACME data found in database");
    goto finish;
  }

  if (!doc->node) {
    RCC(rc, finish, jbl_to_node(doc->raw, &doc->node, true, pool));
  }

  RCC(rc, finish, jbn_at(doc->node, "/skey", &n));
  if (n->type != JBV_STR) {
    rc = IW_ERROR_INVALID_VALUE;
    goto finish;
  }
  private_key = n->vptr;
  private_key_len = n->vsize;

  RCC(rc, finish, jbn_at(doc->node, "/certs", &n));
  if (n->type != JBV_STR) {
    rc = IW_ERROR_INVALID_VALUE;
    goto finish;
  }
  certs = n->vptr;
  certs_len = n->vsize;

  struct iwn_poller *poller = iwn_wf_poller_get(g_env.wf);
  int fd = iwn_wf_server_fd_get(g_env.wf);
  if (!iwn_http_server_ssl_set(poller, fd, &(struct iwn_http_server_ssl_spec) {
    .certs = certs,
    .certs_len = certs_len,
    .certs_in_buffer = true,
    .private_key = private_key,
    .private_key_len = private_key_len,
    .private_key_in_buffer = true,
  })) {
    rc = IW_ERROR_FAIL;
  }

finish:
  jql_destroy(&q);
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_error2(rc, "ACME | Failed to reinit HTTPS endpoint, shutting down");
    gr_shutdown();
  }
  return false;
}

static char* _strnchr(char *src, size_t len, char ch) {
  for (size_t i = 0; i < len; ++i) {
    if (src[i] == ch) {
      return src + i;
    }
  }
  return 0;
}

static int _handler_redirect(struct iwn_wf_req *req) {
  iwrc rc = 0;
  int ret = 500;
  if (!g_env.ssl_enabled) {
    return 0;
  }
  struct iwn_val hv = iwn_http_request_header_get(req->http, "x-forwarded-host", IW_LLEN("x-forwarded-host"));
  if (!hv.len) {
    hv = iwn_http_request_header_get(req->http, "host", IW_LLEN("host"));
    if (!hv.len) {
      if (g_env.domain_name) {
        hv = (struct iwn_val) {
          .len = strlen(g_env.domain_name),
          .buf = (char*) g_env.domain_name
        };
      } else {
        hv = (struct iwn_val) {
          .len = strlen(g_env.host),
          .buf = (char*) g_env.host
        };
      }
    }
  }
  IWXSTR *xstr = iwxstr_new();
  RCB(finish, xstr);
  RCC(rc, finish, iwxstr_cat(xstr, "https://", IW_LLEN("https://")));
  char *cp = _strnchr(hv.buf, hv.len, ':');
  if (cp) {
    RCC(rc, finish, iwxstr_cat(xstr, hv.buf, cp - hv.buf));
  } else {
    RCC(rc, finish, iwxstr_cat(xstr, hv.buf, hv.len));
  }
  if (g_env.port != 443) {
    RCC(rc, finish, iwxstr_printf(xstr, ":%d", g_env.port));
  }
  RCC(rc, finish, iwxstr_cat2(xstr, req->path));
  RCC(rc, finish, iwn_http_response_header_add(req->http, "location", iwxstr_ptr(xstr), iwxstr_size(xstr)));
  ret = iwn_http_response_by_code(req->http, 302) ? 1 : -1;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  iwxstr_destroy(xstr);
  return ret;
}

static int _handler_challenge(struct iwn_wf_req *req, void *d) {
  if (  _challenge_token[0] == '\0' || _challenge_body[0] == '\0'
     || strncmp(req->path, "/.well-known/acme-challenge/", IW_LLEN("/.well-known/acme-challenge/")) != 0) {
    return _handler_redirect(req);
  }
  if (strcmp(_challenge_token, req->path + IW_LLEN("/.well-known/acme-challenge/")) != 0) {
    return 403;
  }
  return iwn_http_response_write(req->http, 200, "application/octet-stream",
                                 _challenge_body, -1) ? 1 : -1;
}

iwrc grh_route_acme_challenge(struct iwn_wf_route *parent) {
  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .handler = _handler_challenge,
    .tag = "acme_challenge"
  }, 0);
}
