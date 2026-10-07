#include "scs_sdk115.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "amtrucks/scssdk_telemetry_ats.h"
#include "common/scssdk_telemetry_common_configs.h"
#include "common/scssdk_telemetry_common_gameplay_events.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "scs-telemetry-common.hpp"

extern scs_timestamp_t simulatedtimestamp;
extern scs_timestamp_t car_carJobCancelled_timer_expire;
extern scs_timestamp_t car_cancelled_cancelledpenalty_timer_expire;

extern scsTelemetryMap_t* telem_ptr;

static_assert(offsetof(scsTelemetryMap_t, trailer) == 6000, "zone 14 moved");
static_assert(sizeof(scsTrailer_t) == 1560, "trailer size changed");
static_assert(offsetof(scsTelemetryMap_t, carJob_ll) == 22000, "zone 15 moved");
static_assert(offsetof(scsTelemetryMap_t, carJob_i) == 22024, "carJob_i moved");
static_assert(offsetof(scsTelemetryMap_t, carJob_b) == 22064, "carJob_b moved");
static_assert(offsetof(scsTelemetryMap_t, carJob_s) == 22072, "carJob_s moved");
static_assert(offsetof(scsTelemetryMap_t, sdk115_channels) == 22744, "sdk115_channels moved");
static_assert(offsetof(scsTelemetryMap_t, busJobConfig) == 22756, "busJobConfig moved");
static_assert(offsetof(scsTelemetryMap_t, otherConfig) == 27184, "otherConfig moved");
static_assert(offsetof(scsTelemetryMap_t, otherGameplayEvent) == 28348, "otherGameplayEvent moved");
static_assert(sizeof(scsGenericAttribute_t) == 136, "generic attribute size changed");
static_assert(sizeof(scsTelemetryMap_t) <= SCS_PLUGIN_MMF_SIZE, "map exceeds the shared memory");

namespace {

// Reads check the value type: reading the wrong union member can crash the game.

void copy_string(char* destination, size_t size, const scs_value_t& value) {
  memset(destination, 0, size);
  if (value.type != SCS_VALUE_TYPE_string || value.value_string.value == nullptr) return;
  strncpy(destination, value.value_string.value, size - 1);
}

unsigned int read_u32(const scs_value_t& value) {
  return value.type == SCS_VALUE_TYPE_u32 ? value.value_u32.value : 0;
}

bool read_bool(const scs_value_t& value) {
  return value.type == SCS_VALUE_TYPE_bool && value.value_bool.value != 0;
}

float read_float(const scs_value_t& value) {
  return value.type == SCS_VALUE_TYPE_float ? value.value_float.value : 0.0f;
}

bool is(const scs_named_value_t* attribute, const char* name) {
  return strcmp(attribute->name, name) == 0;
}

void format_value(char* destination, size_t size, const scs_value_t& value) {
  switch (value.type) {
    case SCS_VALUE_TYPE_bool:
      snprintf(destination, size, "%s", value.value_bool.value ? "true" : "false");
      break;
    case SCS_VALUE_TYPE_s32:
      snprintf(destination, size, "%d", value.value_s32.value);
      break;
    case SCS_VALUE_TYPE_u32:
      snprintf(destination, size, "%u", value.value_u32.value);
      break;
    case SCS_VALUE_TYPE_u64:
      snprintf(destination, size, "%llu", static_cast<unsigned long long>(value.value_u64.value));
      break;
    case SCS_VALUE_TYPE_s64:
      snprintf(destination, size, "%lld", static_cast<long long>(value.value_s64.value));
      break;
    case SCS_VALUE_TYPE_float:
      snprintf(destination, size, "%.9g", value.value_float.value);
      break;
    case SCS_VALUE_TYPE_double:
      snprintf(destination, size, "%.17g", value.value_double.value);
      break;
    case SCS_VALUE_TYPE_fvector:
      snprintf(destination, size, "%.9g %.9g %.9g", value.value_fvector.x, value.value_fvector.y, value.value_fvector.z);
      break;
    case SCS_VALUE_TYPE_dvector:
      snprintf(destination, size, "%.17g %.17g %.17g", value.value_dvector.x, value.value_dvector.y, value.value_dvector.z);
      break;
    case SCS_VALUE_TYPE_euler:
      snprintf(destination, size, "%.9g %.9g %.9g", value.value_euler.heading, value.value_euler.pitch, value.value_euler.roll);
      break;
    case SCS_VALUE_TYPE_fplacement: {
      const auto& p = value.value_fplacement;
      snprintf(destination, size, "%.9g %.9g %.9g %.9g %.9g %.9g", p.position.x, p.position.y, p.position.z,
               p.orientation.heading, p.orientation.pitch, p.orientation.roll);
      break;
    }
    case SCS_VALUE_TYPE_dplacement: {
      const auto& p = value.value_dplacement;
      snprintf(destination, size, "%.17g %.17g %.17g %.9g %.9g %.9g", p.position.x, p.position.y, p.position.z,
               p.orientation.heading, p.orientation.pitch, p.orientation.roll);
      break;
    }
    case SCS_VALUE_TYPE_string:
      copy_string(destination, size, value);
      break;
    default:
      destination[0] = '\0';
      break;
  }
}

template <unsigned int Capacity>
void capture(scsGenericBlock_t<Capacity>& block, const char* id, const scs_named_value_t* attributes) {
  unsigned int count = 0;
  for (const scs_named_value_t* current = attributes; current->name != nullptr; ++current) {
    if (count < Capacity) {
      scsGenericAttribute_t& target = block.attributes[count];
      target.index = current->index;
      target.type = current->value.type;
      memset(target.name, 0, sizeof target.name);
      strncpy(target.name, current->name, sizeof target.name - 1);
      memset(target.value, 0, sizeof target.value);
      format_value(target.value, sizeof target.value, current->value);
    }
    ++count;
  }
  const unsigned int stored = count < Capacity ? count : Capacity;
  memset(&block.attributes[stored], 0, sizeof(scsGenericAttribute_t) * (Capacity - stored));
  memset(block.id, 0, sizeof block.id);
  strncpy(block.id, id, sizeof block.id - 1);
  block.count = count;
  block.stored = stored;
  ++block.sequence;
}

SCSAPI_VOID store_mandatory_break(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
  auto& channels = telem_ptr->sdk115_channels;
  channels.mandatoryBreakHasValue = value != nullptr && value->type == SCS_VALUE_TYPE_s32;
  channels.mandatoryBreak = channels.mandatoryBreakHasValue ? value->value_s32.value : 0;
}

SCSAPI_VOID store_bus_job_average_satisfaction(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
  auto& channels = telem_ptr->sdk115_channels;
  channels.busJobAverageSatisfactionHasValue = value != nullptr && value->type == SCS_VALUE_TYPE_float;
  channels.busJobAverageSatisfaction = channels.busJobAverageSatisfactionHasValue ? value->value_float.value : 0.0f;
}

void clear_car_job_config() {
  auto& ll = telem_ptr->carJob_ll;
  auto& i = telem_ptr->carJob_i;
  auto& b = telem_ptr->carJob_b;
  ll.income = 0;
  i.unitCount = 0;
  i.deliveryTime = 0;
  i.plannedDistanceKm = 0;
  b.customerPrioCargoHandling = false;
  b.customerPrioTime = false;
  b.customerPrioVehicleAppearance = false;
  memset(&telem_ptr->carJob_s, 0, sizeof telem_ptr->carJob_s);
}

}  // namespace

void sdk115_register_channels(const scs_telemetry_init_params_v101_t* params) {
  auto& channels = telem_ptr->sdk115_channels;
  // Documented since patch 1.60 but not registered by the 1.61 games
  if (check_min_version(19, 6)) {
    channels.mandatoryBreakRegistered =
        params->register_for_channel(SCS_TELEMETRY_CHANNEL_next_mandatory_break, SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                                     SCS_TELEMETRY_CHANNEL_FLAG_no_value, store_mandatory_break, nullptr) == SCS_RESULT_ok;
  }
  if (check_min_version(20, 7)) {
    channels.busJobAverageSatisfactionRegistered =
        params->register_for_channel(TSGPS_CHANNEL_bus_job_average_satisfaction, SCS_U32_NIL, SCS_VALUE_TYPE_float,
                                     SCS_TELEMETRY_CHANNEL_FLAG_no_value, store_bus_job_average_satisfaction,
                                     nullptr) == SCS_RESULT_ok;
  }
}

void sdk115_handle_car_job_config(const scs_named_value_t* attributes) {
  clear_car_job_config();
  auto& ll = telem_ptr->carJob_ll;
  auto& i = telem_ptr->carJob_i;
  auto& b = telem_ptr->carJob_b;
  auto& s = telem_ptr->carJob_s;

  const bool has_car_job = attributes->name != nullptr;
  for (const scs_named_value_t* a = attributes; a->name != nullptr; ++a) {
    const scs_value_t& v = a->value;
    if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_id)) copy_string(s.cargoId, sizeof s.cargoId, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo)) copy_string(s.cargo, sizeof s.cargo, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_unit_count)) i.unitCount = read_u32(v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city_id)) copy_string(s.cityDstId, sizeof s.cityDstId, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city)) copy_string(s.cityDst, sizeof s.cityDst, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city_id)) copy_string(s.citySrcId, sizeof s.citySrcId, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city)) copy_string(s.citySrc, sizeof s.citySrc, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_company_id)) copy_string(s.compDstId, sizeof s.compDstId, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_company)) copy_string(s.compDst, sizeof s.compDst, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_company_id)) copy_string(s.compSrcId, sizeof s.compSrcId, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_company)) copy_string(s.compSrc, sizeof s.compSrc, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_income)) ll.income = v.type == SCS_VALUE_TYPE_u64 ? v.value_u64.value : 0;
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_delivery_time)) i.deliveryTime = read_u32(v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_planned_distance_km)) i.plannedDistanceKm = read_u32(v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_car_job_market)) copy_string(s.market, sizeof s.market, v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_customer_prio_cargo_handling)) b.customerPrioCargoHandling = read_bool(v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_customer_prio_time)) b.customerPrioTime = read_bool(v);
    else if (is(a, SCS_TELEMETRY_CONFIG_ATTRIBUTE_customer_prio_vehicle_appearance)) b.customerPrioVehicleAppearance = read_bool(v);
  }

  // The empty car_job arrives before car_job.delivered/cancelled, so the job ends here
  if (has_car_job && !b.onCarJob) {
    b.onCarJob = true;
    i.startingTime = telem_ptr->common_ui.time_abs;
  } else if (!has_car_job && b.onCarJob) {
    b.onCarJob = false;
    i.finishedTime = telem_ptr->common_ui.time_abs;
    b.carJobFinished ^= true;
  }
}

void sdk115_handle_car_job_cancelled(const scs_named_value_t* attributes) {
    auto& ll = telem_ptr->carJob_ll;
    ll.cancelledPenalty = 0;
    for (const scs_named_value_t* a = attributes; a->name != nullptr; ++a) {
        if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_cancel_penalty) && a->value.type == SCS_VALUE_TYPE_s64) {
            ll.cancelledPenalty = a->value.value_s64.value;
        }
    }
    telem_ptr->carJob_b.carJobCancelled = true;
    car_carJobCancelled_timer_expire = simulatedtimestamp + 2000000;
    car_cancelled_cancelledpenalty_timer_expire = simulatedtimestamp + 2000000;
}

void sdk115_handle_car_job_delivered(const scs_named_value_t* attributes) {
  auto& ll = telem_ptr->carJob_ll;
  auto& i = telem_ptr->carJob_i;
  ll.deliveredRevenue = 0;
  i.deliveredEarnedXp = 0;
  i.deliveredCargoDamage = 0.0f;
  i.deliveredVehicleDamage = 0.0f;
  i.deliveredDistanceKm = 0.0f;
  i.deliveredDeliveryTime = 0;
  for (const scs_named_value_t* a = attributes; a->name != nullptr; ++a) {
    const scs_value_t& v = a->value;
    if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_revenue)) ll.deliveredRevenue = v.type == SCS_VALUE_TYPE_s64 ? v.value_s64.value : 0;
    else if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_earned_xp)) i.deliveredEarnedXp = v.type == SCS_VALUE_TYPE_s32 ? v.value_s32.value : 0;
    else if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_cargo_damage)) i.deliveredCargoDamage = read_float(v);
    else if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_vehicle_damage)) i.deliveredVehicleDamage = read_float(v);
    else if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_distance_km)) i.deliveredDistanceKm = read_float(v);
    else if (is(a, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_delivery_time)) i.deliveredDeliveryTime = read_u32(v);
  }
  telem_ptr->carJob_b.carJobDelivered ^= true;
}

void sdk115_capture_config(const char* id, const scs_named_value_t* attributes) {
  if (strcmp(id, SCS_TELEMETRY_CONFIG_bus_job) == 0) {
    capture(telem_ptr->busJobConfig, id, attributes);
  } else {
    capture(telem_ptr->otherConfig, id, attributes);
  }
}

void sdk115_capture_gameplay_event(const char* id, const scs_named_value_t* attributes) {
  capture(telem_ptr->otherGameplayEvent, id, attributes);
}
