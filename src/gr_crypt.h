#pragma once

#include <stddef.h>
#include <stdint.h>

#include <iowow/basedefs.h>

/// @brief Hash a given `input` password and returns salt of hashed password.
/// @note Zero seed returned means error.
///
/// @param input Input password text.
/// @param len Length of input password text.
/// @param salt Salt used for hash. If zero - a new salt will be generated.
/// @param[out] out_hash Output hash.
/// @return uint32_t Hash salt. Zero means error.
///
int64_t gr_crypt_pwhash(const void *input, size_t len, int64_t salt, char out_hash[65]);

void gr_crypt_sha256(const void *input, size_t len, char out_hash[65]);

extern struct curl_blob *cainfo_blob;
