#include "heapmon.h"

#include <esp_heap_caps.h>

namespace heapmon {
namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t bootMin = UINT32_MAX;
bool started = false;
}  // namespace

void begin() {
  // The first start allocates a small table; later starts only reset the minimums.
  bootMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  started = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
}

uint32_t lowSince() {
  // While monitoring, the heap's minimum is the window's minimum.
  portENTER_CRITICAL(&mux);
  uint32_t low = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  if (low < bootMin) bootMin = low;
  if (started) heap_caps_monitor_local_minimum_free_size_start();  // resets each heap's minimum, no allocation
  portEXIT_CRITICAL(&mux);
  return low;
}

uint32_t minEver() {
  portENTER_CRITICAL(&mux);
  uint32_t low = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  if (low < bootMin) bootMin = low;
  uint32_t m = bootMin;
  portEXIT_CRITICAL(&mux);
  return m;
}

}  // namespace heapmon
