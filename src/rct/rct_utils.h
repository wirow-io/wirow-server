#pragma once

#include "rct.h"

void rct_utils_parse_scalability_mode(
  const char *mode,
  int        *spartial_layers_out,
  int        *temporal_layers_out,
  bool       *ksvc_out);

iwrc rct_utils_codecs_is_matched(JBL_NODE ac, JBL_NODE bc, bool strict, bool modify, IWPOOL *pool, bool *res);
