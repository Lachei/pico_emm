/*
 * FreeRTOS V202111.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

#ifndef FREERTOS_CONFIG_EXAMPLES_COMMON_H
#define FREERTOS_CONFIG_EXAMPLES_COMMON_H





// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
// no PICO_DEFAULT_LED_PIN - LED is on Wireless chip
// no PICO_DEFAULT_WS2812_PIN

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif
// Drive high to force power supply into PWM mode (lower ripple on 3V3 at light loads)
// note the SMSP mode pin is on WL_GPIO1

#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT 3
#endif

#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0
#endif

// Drive high to force power supply into PWM mode (lower ripple on 3V3 at light loads)
// As this is a CYW43 pin you can do this by calling cyw43_gpio_set
#ifndef CYW43_WL_GPIO_SMPS_PIN
#define CYW43_WL_GPIO_SMPS_PIN 1
#endif

// If CYW43_WL_GPIO_VBUS_PIN is defined then a CYW43 GPIO has to be used to read VBUS.
// This can be passed to cyw43_arch_gpio_get to determine if the device is battery powered.
// PICO_VBUS_PIN and CYW43_WL_GPIO_VBUS_PIN should not both be defined.
#ifndef CYW43_WL_GPIO_VBUS_PIN
#define CYW43_WL_GPIO_VBUS_PIN 2
#endif

// If CYW43_USES_VSYS_PIN is defined then CYW43 uses the VSYS GPIO (defined by PICO_VSYS_PIN) for other purposes.
// If this is the case, to use the VSYS GPIO it's necessary to ensure CYW43 is not using it.
// This can be achieved by wrapping the use of the VSYS GPIO in cyw43_thread_enter / cyw43_thread_exit.
#ifndef CYW43_USES_VSYS_PIN
#define CYW43_USES_VSYS_PIN 1
#endif

// The GPIO Pin used to monitor VSYS. Typically you would use this with ADC.
// There is an example in adc/read_vsys in pico-examples.
#ifndef PICO_VSYS_PIN
#define PICO_VSYS_PIN 29
#endif

// cyw43 SPI pins can't be changed at runtime
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif

// gpio pin to power up the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 23u
#endif

// gpio pin for spi data out to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 24u
#endif

// gpio pin for spi data in from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 24u
#endif

// gpio (irq) pin for the irq line from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 24u
#endif

// gpio pin for the spi clock line to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 29u
#endif

// gpio pin for the spi chip select to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 25u
#endif










/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE1
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

/* Scheduler Related */
#define configUSE_PREEMPTION                    1
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    12
#define configMINIMAL_STACK_SIZE                ( configSTACK_DEPTH_TYPE ) 256
#define configUSE_16_BIT_TICKS                  0

#define configIDLE_SHOULD_YIELD                 1

/* Synchronization Related */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
// todo need this for lwip FreeRTOS sys_arch to compile
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5
#define configHEAP_CLEAR_MEMORY_ON_FREE         1

/* System */
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        uint32_t

/* Memory allocation related definitions. */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   32 * 1024
#define configAPPLICATION_ALLOCATED_HEAP        0

/* Hook function related definitions. */
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0 /* call vApplicationDaemonTaskStartupHook() when the scheduler is started */

/* Run time and task stats gathering related definitions. */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Co-routine related definitions. */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

/* Software timer related definitions. */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            1024

/* Interrupt nesting behaviour configuration. */
/*
#define configKERNEL_INTERRUPT_PRIORITY         [dependent of processor]
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    [dependent on processor and application]
#define configMAX_API_CALL_INTERRUPT_PRIORITY   [dependent on processor and application]
*/

#define configNUMBER_OF_CORES                   2
/* SMP (configNUMBER_OF_CORES > 1) only */
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           1
#if configNUMBER_OF_CORES > 1
#define configUSE_CORE_AFFINITY                 1
#endif
#define configUSE_PASSIVE_IDLE_HOOK             0

/* Armv8-M */

/* Not currently supported */
#define configENABLE_MPU                        0
//#define configSYSTEM_CALL_STACK_SIZE            ( configSTACK_DEPTH_TYPE ) 512
#define configENABLE_FPU                        1
/* Not currently supported */
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1
// see https://www.freertos.org/RTOS-Cortex-M3-M4.html
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    16

/* RP2xxx specific */
#define configSUPPORT_PICO_SYNC_INTEROP         1
#define configSUPPORT_PICO_TIME_INTEROP         1

#include <assert.h>
/* Define to trap errors during development. */
#define configASSERT(x)                         assert(x)

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_xQueueGetMutexHolder            1

/* A header file that defines trace macro can be included here. */

#endif /* FREERTOS_CONFIG_H */

