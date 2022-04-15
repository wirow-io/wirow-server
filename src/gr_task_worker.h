#pragma once
#include "grh.h"

/// Postprocess room recording
#define PT_RECORDING_POSTPROC 0x01

/// Submits persistent durable task of given `type`.
///
/// The following task types are supported:
///
/// * PT_RECORDING_POSTPROC
///
iwrc gr_persistent_task_submit(int type, const char *hook, JBL spec);

iwrc gr_periodic_worker_init(void);

