#include "grh_resources_pub.h"
#include "grh_resources_impl.h"

#include <iowow/iwhmap.h>
#include <iowow/iwxstr.h>

#include <iwnet/iwn_wf_files.h>
#include <iwnet/iwn_mimetypes.h>

#include "lic_env.h"

#include "data_favicon_ico.inc"
#include "data_robots_txt.inc"

#include "data_front_1_woff2.inc"
#include "data_front_2_woff2.inc"
#include "data_front_3_woff2.inc"
#include "data_front_4_woff2.inc"
#include "data_front_5_woff2.inc"
#include "data_front_6_woff2.inc"
#include "data_front_7_woff2.inc"
#include "data_front_8_woff2.inc"
#include "data_front_9_woff2.inc"
#include "data_front_10_woff2.inc"
#include "data_front_11_woff2.inc"
#include "data_front_12_woff2.inc"
#include "data_front_fonts_css.inc"
#include "data_front_image_bg4_svg.inc"

#include "data_notify_new_member_mp3.inc"
#include "data_notify_recording_started_mp3.inc"
#include "data_notify_chat_message_mp3.inc"

#include "data_front_public_js_gz.inc"
#include "data_front_bundle_css_gz.inc"

#if (ENABLE_WHITEBOARD == 1)
#include "data_front_whiteboard_index.inc"
#include "data_front_whiteboard_main_js_gz.inc"
#include "data_front_whiteboard_main_css_gz.inc"
#include "data_front_whiteboard_excalidraw_image_js_gz.inc"
#include "data_front_whiteboard_excalidraw_vendor_js_gz.inc"
#include "data_front_whiteboard_excalidraw_virgil_woff2.inc"
#include "data_front_whiteboard_excalidraw_cascadia_woff2.inc"
#endif

static struct resource _res[] = {
  RES("/favicon.ico",
      0x68322ea3,
      "image/x-icon",
      data_favicon_ico,
      false,
      true),

  RES("/robots.txt",
      0x9e9f267b,
      "text/plain",
      data_robots_txt,
      false,
      true),

  RES("/public.js",
      0x5f81cd9e,
      "application/javascript;charset=UTF-8",
      data_front_public_js_gz,
      true,
      false),

  RES("/bundle.css",
      0xc98f33a5,
      "text/css;charset=UTF-8",
      data_front_bundle_css_gz,
      true,
      false),

  RES("/fonts.css",
      0xf8174bf5,
      "text/css;charset=UTF-8",
      data_front_fonts_css,
      false,
      true),

  RES("/fonts/1.woff2",
      0xcd0b4190,
      "font/woff2",
      data_front_1_woff2,
      false,
      true),

  RES("/fonts/10.woff2",
      0x5eb51180,
      "font/woff2",
      data_front_10_woff2,
      false,
      true),

  RES("/fonts/11.woff2",
      0xabaf4e41,
      "font/woff2",
      data_front_11_woff2,
      false,
      true),

  RES("/fonts/12.woff2",
      0xf8a98b02,
      "font/woff2",
      data_front_12_woff2,
      false,
      true),

  RES("/fonts/2.woff2",
      0x1a057e51,
      "font/woff2",
      data_front_2_woff2,
      false,
      true),

  RES("/fonts/3.woff2",
      0x66ffbb12,
      "font/woff2",
      data_front_3_woff2,
      false,
      true),

  RES("/fonts/4.woff2",
      0xb3f9f7d3,
      "font/woff2",
      data_front_4_woff2,
      false,
      true),

  RES("/fonts/5.woff2",
      0xf43494,
      "font/woff2",
      data_front_5_woff2,
      false,
      true),

  RES("/fonts/6.woff2",
      0x4dee7155,
      "font/woff2",
      data_front_6_woff2,
      false,
      true),

  RES("/fonts/7.woff2",
      0x9ae8ae16,
      "font/woff2",
      data_front_7_woff2,
      false,
      true),

  RES("/fonts/8.woff2",
      0xe7e2ead7,
      "font/woff2",
      data_front_8_woff2,
      false,
      true),

  RES("/fonts/9.woff2",
      0x34dd2798,
      "font/woff2",
      data_front_9_woff2,
      false,
      true),

  RES("/audio/notify_new_member.mp3",
      0xb3bb6ec,
      "audio/mpeg",
      data_notify_new_member_mp3,
      false,
      true),

  RES("/audio/notify_recording_started.mp3",
      0xe56179be,
      "audio/mpeg",
      data_notify_recording_started_mp3,
      false,
      true),

  RES("/audio/notify_chat_message.mp3",
      0x49cdcaf,
      "audio/mpeg",
      data_notify_chat_message_mp3,
      false,
      true),

  RES("/images/bg4.svg",
      0x98933894,
      "image/svg+xml",
      data_front_image_bg4_svg,
      false,
      true),

#if (ENABLE_WHITEBOARD == 1)
  RES("/whiteboard/",
      0x17f637cc,
      "text/html",
      data_front_whiteboard_index,
      false,
      false),

  RES("/whiteboard/index.html",
      0xaded11c7,
      "text/html",
      data_front_whiteboard_index,
      false,
      false),

  RES("/whiteboard/main.js",
      0xe0e16ddc,
      "application/javascript;charset=UTF-8",
      data_front_whiteboard_main_js_gz,
      true,
      false),

  RES("/whiteboard/main.css",
      0xfd0f0c08,
      "text/css;charset=UTF-8",
      data_front_whiteboard_main_css_gz,
      true,
      false),

  RES("/whiteboard/excalidraw-assets/image.js",
      0x47c4062d,
      "application/javascript;charset=UTF-8",
      data_front_whiteboard_excalidraw_image_js_gz,
      true,
      false),

  RES("/whiteboard/excalidraw-assets/vendor.js",
      0xab16bc98,
      "application/javascript;charset=UTF-8",
      data_front_whiteboard_excalidraw_vendor_js_gz,
      true,
      false),

  RES("/whiteboard/excalidraw-assets/Virgil.woff2",
      0x50e9b87e,
      "font/woff2",
      data_front_whiteboard_excalidraw_virgil_woff2,
      false,
      true),

  RES("/whiteboard/excalidraw-assets/Cascadia.woff2",
      0xe80ab53a,
      "font/woff2",
      data_front_whiteboard_excalidraw_cascadia_woff2,
      false,
      true),
#endif
};

static int _handler_resources(struct iwn_wf_req *req, void *data) {
  IWHMAP *m = g_env.public_overlays;
  if (m) {
    const char *path = iwhmap_get(m, req->path);
    if (path) {
      const char *ctype = iwn_mimetype_find_by_path(path);
      return iwn_wf_file_serve(req, ctype, path);
    }
  }
  uint32_t hash = _hash(req->path);
  for (int i = 0; i < sizeof(_res) / sizeof(_res[0]); ++i) {
    struct resource *r = &_res[i];
    if (r->hash == hash && strcmp(req->path, r->path) == 0) {
      return _serve(r, req);
    }
  }
  return 0;
}

iwrc grh_route_resources_pub(const struct iwn_wf_route *parent) {
  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .handler = _handler_resources,
    .flags = IWN_WF_GET,
    .tag = "resources_pub"
  }, 0);
}
