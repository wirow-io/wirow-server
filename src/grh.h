#pragma once

#include "gr.h"
#include "iwnet_extra.h"

extern struct gr_env g_env;

#define Q(s)    #s
#define P(s, v) "\""Q (s) "\":"#v
#define N(s, v) P(s, v) ","

#define REQ_FLAG_ROLE_USER  0x01U
#define REQ_FLAG_ROLE_ADMIN 0x02U
#define REQ_FLAG_AUTH_OK    0x04U
#define REQ_FLAG_ROLE_ALL   (REQ_FLAG_ROLE_USER | REQ_FLAG_ROLE_ADMIN)
