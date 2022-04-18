#pragma once
/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include <stdint.h>

/**
 * @brief Detect network IP address of machine.
 *
 * Returns allocated string or zero in the case of error or no addresses found.
 */
char *network_detect_best_listen_ip_address(void);

/**
 * @brief Finds unused port number from ephemeral port range.
 *
 * Returns zero in the case of error and errno is set appropriately
 * @param ip Optional IP address of socket.
 */
uint16_t network_detect_unused_port(const char *ip);
