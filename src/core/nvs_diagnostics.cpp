#include "nvs_diagnostics.h"

#include <Arduino.h>
#include <nvs.h>

void logNvsStats() {
  nvs_stats_t stats{};
  const esp_err_t result = nvs_get_stats(nullptr, &stats);
  if (result != ESP_OK) {
    Serial.printf("[NVS] stats error=0x%X\n", static_cast<unsigned>(result));
    return;
  }
  Serial.printf("[NVS] used=%lu free=%lu total=%lu namespaces=%lu\n",
                static_cast<unsigned long>(stats.used_entries),
                static_cast<unsigned long>(stats.free_entries),
                static_cast<unsigned long>(stats.total_entries),
                static_cast<unsigned long>(stats.namespace_count));
}
