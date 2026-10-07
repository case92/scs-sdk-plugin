#ifndef SCS_SDK115_HPP
#define SCS_SDK115_HPP

#include "scssdk_telemetry.h"

#define TSGPS_CHANNEL_bus_job_average_satisfaction "bus_job.average_satisfaction"

void sdk115_register_channels(const scs_telemetry_init_params_v101_t* params);

void sdk115_handle_car_job_config(const scs_named_value_t* attributes);
void sdk115_handle_car_job_cancelled(const scs_named_value_t* attributes);
void sdk115_handle_car_job_delivered(const scs_named_value_t* attributes);

void sdk115_capture_config(const char* id, const scs_named_value_t* attributes);
void sdk115_capture_gameplay_event(const char* id, const scs_named_value_t* attributes);

#endif
