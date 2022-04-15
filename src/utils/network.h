#pragma once

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
