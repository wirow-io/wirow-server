#pragma once
#include "gr.h"

iwrc gr_db_init(void);

iwrc gr_db_user_create(const char *name, const char *pw, int64_t salt, char *pwh, bool hidden);
