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
