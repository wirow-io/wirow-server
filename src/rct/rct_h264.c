#include "rct_h264.h"
#include "rct.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

typedef struct _profile_pattern {
  h264_profile_e profile;
  uint8_t ldc;
  uint8_t mask;
  uint8_t masked_value;
} _profile_pattern_t;

static h264_plid_t _default_plid = {
  .profile = PROFILE_CONSTRAINED_BASELINE,
  .level   = LEVEL_3_1
};

static _profile_pattern_t _profile_patterns[] = {
  { PROFILE_CONSTRAINED_BASELINE, 0x42, 0x4f, 0x40 }, // x1xx0000
  { PROFILE_CONSTRAINED_BASELINE, 0x4D, 0x8f, 0x80 }, // 1xxx0000
  { PROFILE_CONSTRAINED_BASELINE, 0x58, 0xcf, 0xc0 }, // 11xx0000
  { PROFILE_BASELINE,             0x42, 0x4f, 0x00 }, // x0xx0000
  { PROFILE_BASELINE,             0x58, 0xcf, 0x80 }, // 10xx0000
  { PROFILE_MAIN,                 0x4D, 0xaf, 0x00 }, // 0x0x0000
  { PROFILE_HIGH,                 0x64, 0xff, 0x00 }, // 00000000
  { PROFILE_CONSTRAINED_HIGH,     0x64, 0xff, 0x0c }  // 00001100
};

// For level_idc=11 and profile_idc=0x42, 0x4D, or 0x58, the constraint set3
// flag specifies if level 1b or level 1.1 is used.
static const int constraint_set3_flag = 0x10;

iwrc h264_plid_parse(const char spec[static 6], h264_plid_t *out) {
  // 4d0032
  char nspec[7];
  memcpy(nspec, spec, 6);
  nspec[6] = '\0';

  memset(out, 0, sizeof(*out));
  unsigned long int numspec = strtol(nspec, 0, 16);
  if ((numspec < 1) || (errno && ((numspec == LONG_MAX) || (numspec == LONG_MIN)))) {
    return RCT_ERROR_INVALID_PROFILE_LEVEL_ID;
  }
  uint8_t level_idc = numspec & 0xff;
  uint8_t profile_iop = (numspec >> 8) & 0xff;
  uint8_t profile_idc = (numspec >> 16) & 0xff;
  switch (level_idc) {
    case LEVEL_1_1:
      out->level = (profile_iop & constraint_set3_flag) ? LEVEL_1B : LEVEL_1_1;
      break;
    case LEVEL_1:
    case LEVEL_1_2:
    case LEVEL_1_3:
    case LEVEL_2:
    case LEVEL_2_1:
    case LEVEL_2_2:
    case LEVEL_3:
    case LEVEL_3_1:
    case LEVEL_3_2:
    case LEVEL_4:
    case LEVEL_4_1:
    case LEVEL_4_2:
    case LEVEL_5:
    case LEVEL_5_1:
    case LEVEL_5_2:
      out->level = level_idc;
      break;
    default:
      return RCT_ERROR_INVALID_PROFILE_LEVEL_ID;
  }
  // return this._maskedValue === (value & this._mask);
  for (int i = 0; i < sizeof(_profile_patterns) / sizeof(_profile_patterns[0]); ++i) {
    _profile_pattern_t *pp = &_profile_patterns[i];
    if ((pp->ldc == profile_idc) && (pp->masked_value == (profile_iop & pp->mask))) {
      out->profile = pp->profile;
      return 0;
    }
  }
  out->profile = 0; // Not found
  return RCT_ERROR_INVALID_PROFILE_LEVEL_ID;
}

bool h264_plid_equal(const char *p1, size_t p1len, const char *p2, size_t p2len) {
  iwrc rc = 0;
  h264_plid_t pl1, pl2;
  if ((p1len == p2len) && (p1len == 6) && !strncmp(p1, p2, p1len)) {
    return true;
  }
  if (p1len == 6) {
    rc = h264_plid_parse(p1, &pl1);
  } else if (!p1len) {
    pl1 = _default_plid;
  }
  if (p2len == 6) {
    rc = h264_plid_parse(p2, &pl2);
  } else if (!p2len) {
    pl2 = _default_plid;
  }
  return !rc && pl1.profile == pl2.profile;
}

static bool _is_level_asymmetry_allowed(JBL_NODE params) {
  JBL_NODE r;
  iwrc rc = jbn_at(params, "/level-asymmetry-allowed", &r);
  if (rc) {
    return false;
  }
  if (r->type == JBV_I64) {
    return r->vi64 == 1;
  }
  if (r->type == JBV_STR) {
    return strncmp(r->vptr, "1", 1);
  }
  if (r->type == JBV_BOOL) {
    return r->vbool;
  }
  return false;
}

static bool _level_is_lesser_than(h264_level_e a, h264_level_e b) {
  if (a == LEVEL_1B) {
    return b > LEVEL_1;
  }
  if (b == LEVEL_1B) {
    return a != LEVEL_1;
  }
  return a < b;
}

static h264_level_e _level_min(h264_level_e a, h264_level_e b) {
  return _level_is_lesser_than(a, b) ? a : b;
}

iwrc h256_plid_to_answer(JBL_NODE local_params, JBL_NODE remote_params, h264_plid_t *out) {
  iwrc rc;
  if (!out) {
    return IW_ERROR_INVALID_ARGS;
  }
  memset(out, 0, sizeof(*out));
  if (  !local_params || (local_params->type != JBV_OBJECT)
     || !remote_params || (remote_params->type != JBV_OBJECT)) {
    return IW_ERROR_INVALID_ARGS;
  }
  JBL_NODE n1, n2;
  h264_plid_t local_plid, remote_plid;

  rc = jbn_at(local_params, "/profile-level-id", &n1);
  if (rc || (n1->type != JBV_STR) || (n1->vsize != 6)) {
    n1 = 0;
  }
  rc = jbn_at(remote_params, "/profile-level-id", &n2);
  if (rc || (n2->type != JBV_STR) || (n2->vsize != 6)) {
    n2 = 0;
  }
  if (!n1 && !n2) {
    return 0;
  }
  if (n1) {
    RCR(h264_plid_parse(n1->vptr, &local_plid));
  } else {
    local_plid = _default_plid;
  }
  if (n2) {
    RCR(h264_plid_parse(n2->vptr, &remote_plid));
  } else {
    remote_plid = _default_plid;
  }
  if (local_plid.profile != remote_plid.profile) {
    return RCT_ERROR_INVALID_RTP_PARAMETERS;
  }
  bool level_asymmetry_allowed = _is_level_asymmetry_allowed(local_params)
                                 && _is_level_asymmetry_allowed(remote_params);

  h264_level_e local_level = local_plid.level;
  h264_level_e remote_level = remote_plid.level;
  h264_level_e min_level = _level_min(local_level, remote_level);

  out->level = level_asymmetry_allowed ? local_level : min_level;
  out->profile = local_plid.profile;
  return 0;
}

iwrc h256_plid_write(const h264_plid_t *plid, char buf[static 6]) {
  const char *iop;
  char *wp = buf;
  char xlevel[3];
  if (!plid) {
    return IW_ERROR_INVALID_ARGS;
  }
  if (plid->level == LEVEL_1B) {
    switch (plid->profile) {
      case PROFILE_CONSTRAINED_BASELINE: {
        const char *v = "42f00b";
        memcpy(buf, v, 6);
        return 0;
      }
      case PROFILE_BASELINE: {
        const char *v = "42100b";
        memcpy(buf, v, 6);
        return 0;
      }
      case PROFILE_MAIN: {
        const char *v = "4d100b";
        memcpy(buf, v, 6);
        return 0;
      }
      default:
        return RCT_ERROR_INVALID_PROFILE_LEVEL_ID;
    }
  }
  switch (plid->profile) {
    case PROFILE_CONSTRAINED_BASELINE:
      iop = "42e0";
      break;
    case PROFILE_BASELINE:
      iop = "4200";
      break;
    case PROFILE_MAIN:
      iop = "4d00";
      break;
    case PROFILE_CONSTRAINED_HIGH:
      iop = "640c";
      break;
    case PROFILE_HIGH:
      iop = "6400";
      break;
    default:
      return RCT_ERROR_INVALID_PROFILE_LEVEL_ID;
  }
  snprintf(xlevel, sizeof(xlevel), "%02x", (int) plid->level);
  memcpy(wp, iop, 4);
  wp += 4;
  memcpy(wp, xlevel, 2);
  return 0;
}
