#include "rct_utils.h"
#include "rct_h264.h"

#include <iowow/iwre.h>
#include <iowow/iwconv.h>

#include <string.h>

void rct_utils_parse_scalability_mode(
  const char *mode,
  int        *spartial_layers_out,
  int        *temporal_layers_out,
  bool       *ksvc_out
  ) {
  if (spartial_layers_out) {
    *spartial_layers_out = 1;
  }
  if (temporal_layers_out) {
    *temporal_layers_out = 1;
  }
  if (ksvc_out) {
    *ksvc_out = false;
  }
  if (!mode) {
    return;
  }

  char buf[3];
  const char *mpairs[IWRE_MAX_MATCHES];
  struct iwre *re = iwre_create("[LS]([1-9][0-9]?)T([1-9][0-9]?)(_KEY)?");
  if (!re) {
    return;
  }
  int nm = iwre_match(re, mode, mpairs, IWRE_MAX_MATCHES);
  if (nm < 3) {
    goto finish;
  }

  if (spartial_layers_out) {
    if (mpairs[3] - mpairs[2] < sizeof(buf)) {
      memcpy(buf, mpairs[2], mpairs[3] - mpairs[2]);
      buf[mpairs[3] - mpairs[2]] = '\0';
      *spartial_layers_out = (int) iwatoi(buf);
    } else {
      goto finish;
    }
  }

  if (temporal_layers_out) {
    if (mpairs[5] - mpairs[4] < sizeof(buf)) {
      memcpy(buf, mpairs[4], mpairs[5] - mpairs[4]);
      buf[mpairs[5] - mpairs[4]] = '\0';
      *temporal_layers_out = (int) iwatoi(buf);
    } else {
      goto finish;
    }
  }
  if (ksvc_out && nm >= 4) {
    *ksvc_out = mpairs[7] - mpairs[6] > 0;
  }

finish:
  iwre_destroy(re);
}

iwrc rct_utils_codecs_is_matched(JBL_NODE ac, JBL_NODE bc, bool strict, bool modify, IWPOOL *pool, bool *res) {
  *res = false;
  iwrc rc = 0;
  JBL_NODE n1, n2;
  int64_t iv1, iv2;
  const char *mime_type;

  // Compare mime types
  rc = jbn_at(ac, "/mimeType", &n1);
  if (rc || (n1->type != JBV_STR)) {
    return 0;
  }
  rc = jbn_at(bc, "/mimeType", &n2);
  if (rc || (n2->type != n1->type) || (n2->vsize != n1->vsize) || utf8ncasecmp(n1->vptr, n2->vptr, n1->vsize)) {
    return 0;
  }
  mime_type = n1->vptr;

  if (jbn_path_compare(ac, bc, "/clockRate", 0, &rc)) {
    return 0;
  }
  if (jbn_path_compare(ac, bc, "/channels", 0, &rc)) {
    return 0;
  }

  // Checks specific to some codecs
  if (!strcasecmp(mime_type, "video/h264")) {
    iv1 = 0, iv2 = 0;
    rc = jbn_at(ac, "/parameters/packetization-mode", &n1);
    if (!rc && (n1->type == JBV_I64)) {
      iv1 = n1->vi64;
    }
    rc = jbn_at(bc, "/parameters/packetization-mode", &n2);
    if (!rc && (n2->type == JBV_I64)) {
      iv2 = n2->vi64;
    }
    if (iv1 != iv2) {
      return 0;
    }
    ;
    if (strict) {
      h264_plid_t selected_plid;
      rc = jbn_at(ac, "/parameters/profile-level-id", &n1);
      if (rc || (n1->type != JBV_STR)) {
        n1 = 0;
      }
      rc = jbn_at(bc, "/parameters/profile-level-id", &n2);
      if (rc || (n2->type != JBV_STR)) {
        n2 = 0;
      }
      if (!h264_plid_equal(n1 ? n1->vptr : 0, n1 ? n1->vsize : 0, n2 ? n2->vptr : 0, n2 ? n2->vsize : 0)) {
        return 0;
      }
      rc = jbn_at(ac, "/parameters", &n1);
      if (rc) {
        return 0;
      }
      jbn_at(bc, "/parameters", &n2);
      if (rc) {
        return 0;
      }
      rc = h256_plid_to_answer(n1, n2, &selected_plid);
      if (rc) {
        return 0;
      }
      if (modify) {
        if (selected_plid.profile) {
          char plevel[6];
          RCR(h256_plid_write(&selected_plid, plevel));
          RCR(jbn_copy_path(&(struct _JBL_NODE) {
            .type = JBV_STR,
            .vsize = sizeof(plevel),
            .vptr = plevel
          }, "/", ac, "/parameters/profile-level-id", false, false, pool));
        } else {
          jbn_detach(ac, "/parameters/profile-level-id");
        }
      }
    }
  } else if (!strcasecmp(mime_type, "video/vp9")) {
    if (strict) {
      iv1 = 0, iv2 = 0;
      rc = jbn_at(ac, "/parameters/profile-id", &n1);
      if (!rc && (n1->type == JBV_I64)) {
        iv1 = n1->vi64;
      }
      rc = jbn_at(bc, "/parameters/profile-id", &n2);
      if (!rc && (n2->type == JBV_I64)) {
        iv2 = n2->vi64;
      }
      if (iv1 != iv2) {
        return 0;
      }
    }
  }
  *res = true;
  return 0;
}
