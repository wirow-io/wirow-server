#pragma once
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
