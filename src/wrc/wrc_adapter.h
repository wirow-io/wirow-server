#pragma once

#include "wrc.h"

struct wrc_adapter_spec {
  void *user_data;
  void  (*on_msg)(wrc_resource_t wid, const char *cmd, size_t cmd_len, void *user_data);
  void  (*on_payload)(wrc_resource_t wid, const char *payload, size_t payload_len, void *user_data);
  void  (*on_closed)(wrc_resource_t wid, void *user_data, const char *note);
};

iwrc wrc_adapter_create(
  const struct wrc_adapter_spec *spec,
  wrc_resource_t                *out_wrc_id);

iwrc wrc_adapter_send_msg(wrc_resource_t wid, const void *buf, size_t len);

iwrc wrc_adapter_send_payload(wrc_resource_t wid, const void *buf, size_t len);

void wrc_adapter_close(wrc_resource_t wid);

iwrc wrc_adapter_module_init(void);

void wrc_adapter_module_shutdown(void);

void wrc_adapter_module_destroy(void);
