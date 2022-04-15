#include "grh_adm.h"
#include "grh_adm_users.h"
#include "gr_gauges.h"

iwrc grh_route_adm(const struct iwn_wf_route *parent) {
  iwrc rc;
  struct iwn_wf_route *user, *gauges;

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/user",
    .flags = IWN_WF_METHODS_ALL,
  }, &user));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/gauges"
  }, &gauges));

  RCC(rc, finish, grh_route_adm_users(user));
  RCC(rc, finish, grh_route_gauges(gauges));

finish:
  return rc;
}
