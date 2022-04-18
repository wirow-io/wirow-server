#pragma once
/*
 * copyright (c) 2022 greenrooms, inc.
 *
 * this program is free software: you can redistribute it and/or modify it under
 * the terms of the gnu affero general public license as published by the free
 * software foundation, either version 3 of the license, or (at your option) any
 * later version.
 *
 * this program is distributed in the hope that it will be useful, but without
 * any warranty; without even the implied warranty of merchantability or fitness
 * for a particular purpose. see the gnu affero general public license for more
 * details.
 *
 * you should have received a copy of the gnu affero general public license
 * along with this program.  if not, see http://www.gnu.org/licenses/
 */

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
