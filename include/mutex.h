#pragma once

#include "FreeRTOS.h"
#include "semphr.h"

#include "log_storage.h"

struct mutex {
	SemaphoreHandle_t handle{};
	mutex(): handle{xSemaphoreCreateBinary()} { if (!handle || pdTRUE != xSemaphoreGive(handle)) LogError("Failed creating the semaphore");}
	~mutex() {
		if (handle) {
			xSemaphoreGive(handle); // safety give to unblock waiting thread
			vSemaphoreDelete(handle);
		}
	}
};

struct scoped_lock {
	SemaphoreHandle_t handle{};
	scoped_lock(const mutex &m): handle{m.handle} {
		if (!handle || pdFALSE == xSemaphoreTake(handle, portMAX_DELAY)) {
			LogError("Failed to lock mutex");
			handle = NULL;
		}
	}
	~scoped_lock() { if (handle) xSemaphoreGive(handle); }
};

