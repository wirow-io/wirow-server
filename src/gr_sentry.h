#pragma once

#include <iwnet/iwn_wf.h>

iwrc gr_sentry_init(int argc, char **argv);

iwrc grh_route_sentry(struct iwn_wf_route *parent);
