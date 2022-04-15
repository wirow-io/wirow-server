#pragma once
#include "rct.h"

iwrc rct_worker_module_init(void);
iwrc rct_producer_export_module_init(void);
iwrc rct_consumer_module_init(void);
iwrc rct_room_module_init(void);

void rct_worker_module_shutdown(void);
void rct_worker_module_close(void);
void rct_producer_export_module_close(void);
void rct_consumer_module_close(void);
void rct_room_module_close(void);

void rct_router_close_lk(rct_router_t*);
void rct_transport_close_lk(rct_transport_t*);
void rct_transport_dispose_lk(rct_transport_t*);
void rct_producer_close_lk(rct_producer_base_t*);
void rct_producer_dispose_lk(rct_producer_base_t*);
void rct_consumer_dispose_lk(rct_consumer_base_t*);

