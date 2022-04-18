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

/// License error codes

#define LICENSE_CORRUPTED_OR_CLASHED "license.corrupted.or.clashed"
#define LICENSE_INACTIVE             "license.inactive"
#define LICENSE_SUPPORT_REQUIRED     "license.support.required"
#define LICENSE_UNKNOWN              "license.unknown"
#define LICENSE_VERIFICATION_FAILED  "license.verification.failed"

iwrc gr_lic_init(void);

void gr_lic_error(const char **out);

void gr_lic_warning(const char **out);

const char *gr_lic_summary(void);
