#pragma once

#include <cstddef>

extern "C" {
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#if __has_include("freertos/idf_additions.h")
extern "C" {
#include "freertos/idf_additions.h"
}
#define ESP_SCHEDULER_HAS_IDF_TASK_CAPS 1
#else
#define ESP_SCHEDULER_HAS_IDF_TASK_CAPS 0
#endif

#if ESP_SCHEDULER_HAS_IDF_TASK_CAPS && defined(configSUPPORT_STATIC_ALLOCATION) &&                 \
    (configSUPPORT_STATIC_ALLOCATION == 1) && defined(MALLOC_CAP_SPIRAM)
#define ESP_SCHEDULER_CAN_USE_EXTERNAL_STACKS 1
#else
#define ESP_SCHEDULER_CAN_USE_EXTERNAL_STACKS 0
#endif

namespace scheduler_task_support {
constexpr size_t kMinStackSizeBytes = 1024;
#if defined(MALLOC_CAP_SPIRAM)
constexpr UBaseType_t kExternalStackCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
constexpr UBaseType_t kExternalStackCaps = MALLOC_CAP_8BIT;
#endif

inline bool hasExternalStackSupport() {
#if ESP_SCHEDULER_CAN_USE_EXTERNAL_STACKS
	return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
	return false;
#endif
}

inline bool isValidStackSize(size_t stackBytes) {
	return stackBytes >= kMinStackSizeBytes && (stackBytes % sizeof(StackType_t)) == 0;
}

inline BaseType_t createTaskPinned(
    TaskFunction_t entry,
    const char *name,
    size_t stackBytes,
    void *arg,
    UBaseType_t priority,
    TaskHandle_t *handle,
    BaseType_t coreId,
    bool usePsramStack,
    bool &createdWithCaps
) {
	createdWithCaps = false;
	if (!isValidStackSize(stackBytes)) {
		return pdFAIL;
	}
	if (usePsramStack) {
#if ESP_SCHEDULER_CAN_USE_EXTERNAL_STACKS
		if (!hasExternalStackSupport()) {
			return pdFAIL;
		}
		BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
		    entry,
		    name,
		    static_cast<configSTACK_DEPTH_TYPE>(stackBytes),
		    arg,
		    priority,
		    handle,
		    coreId,
		    kExternalStackCaps
		);
		createdWithCaps = (created == pdPASS);
		return created;
#else
		return pdFAIL;
#endif
	}
	return xTaskCreatePinnedToCore(
	    entry,
	    name,
	    static_cast<uint32_t>(stackBytes),
	    arg,
	    priority,
	    handle,
	    coreId
	);
}

inline void deleteTask(TaskHandle_t taskHandle, bool withCaps) {
	if (!taskHandle) {
		return;
	}
#if ESP_SCHEDULER_CAN_USE_EXTERNAL_STACKS
	if (withCaps) {
		vTaskDeleteWithCaps(taskHandle);
		return;
	}
#endif
	vTaskDelete(taskHandle);
}

inline void deleteCurrentTask(bool withCaps) {
#if ESP_SCHEDULER_CAN_USE_EXTERNAL_STACKS
	if (withCaps) {
		vTaskDeleteWithCaps(xTaskGetCurrentTaskHandle());
		return;
	}
#endif
	vTaskDelete(nullptr);
}
} // namespace scheduler_task_support
