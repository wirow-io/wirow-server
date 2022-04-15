#pragma once

#include "gr.h"
#include <iwnet/brssl.h>

struct acme_context {
  private_key *pk;
  br_x509_certificate *certs;
  size_t certs_num;
};

#define ACME_CREATE_ACC     0x01
#define ACME_RENEW          0x02
#define ACME_SYNC_ACC_FORCE 0x04

void acme_context_clean(struct acme_context *ctx);

iwrc acme_sync(uint32_t flags, struct acme_context *out_ctx);

iwrc acme_sync_consumer(uint32_t flags, bool (*consumer)(struct acme_context*));

iwrc acme_sync_consumer_detached(uint32_t flags, bool (*consumer)(struct acme_context*));

bool acme_consumer_callback(struct acme_context *ctx);

iwrc grh_route_acme_challenge(struct iwn_wf_route *parent);
