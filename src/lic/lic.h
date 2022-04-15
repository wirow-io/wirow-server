#pragma once
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
