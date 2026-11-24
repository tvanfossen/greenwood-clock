/**
 * @file log_async.c
 * @version 1
 * @brief Non-blocking ESP log output: callers enqueue formatted lines, a writer task emits them.
 *
 * Measured on device (2026-11-17): log lines from a display transition landed ~8-14 ms
 * apart, i.e. the UART console at 115200 baud (plus the SD mirror) ran synchronously in
 * the logging task. The FSM logs while holding the LVGL lock, so every transition froze
 * the display for (lines x ~8 ms): 8 forecast-strip lines alone were 61-66 ms.
 *
 * Callers now only format into a ring buffer; a low-priority writer task forwards each
 * line to the previous hook chain (debug_log SD mirror -> console). ISR context, the
 * pre-scheduler phase and the writer's own logging stay synchronous. A panic loses only
 * lines still queued; the panic itself is captured in RTC memory and the coredump.
 */
#include "log_async.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

static const char *TAG = "log_async";

#define LOG_ASYNC_RING_BYTES   (32 * 1024)   // SPIRAM; absorbs boot/transition bursts
#define LOG_ASYNC_TASK_STACK   (6 * 1024)    // writer runs the SD mirror (FATFS) + console
#define LOG_ASYNC_TASK_PRIO    2             // below the UI/FSM/network tasks

static RingbufHandle_t s_rb;
static TaskHandle_t    s_writer;
static vprintf_like_t  s_prev;
static atomic_uint     s_dropped;

/**
 * @brief Forward a fully formatted line to the previous vprintf hook (printf-style shim).
 * @version 1
 * @utility
 * @return The previous hook's return value.
 */
static int forward_prev(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = s_prev ? s_prev(fmt, args) : 0;
    va_end(args);
    return r;
}

/**
 * @brief True when this call must stay synchronous: no ring yet, ISR, scheduler not
 *        running, or the writer task itself (its own logging must not re-enqueue).
 * @version 1
 * @utility
 * @return true to bypass the ring buffer.
 */
static bool must_log_sync(void)
{
    return !s_rb || xPortInIsrContext() ||
           xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
           xTaskGetCurrentTaskHandle() == s_writer;
}

/**
 * @brief Log hook: format the line directly into ring-buffer space (no stack buffer, no
 *        truncation) without ever blocking; drop and count it if the ring is full.
 * @version 1
 * @req REQ-LOG-005
 * @callback
 * @return Formatted length, or the previous hook's result on the synchronous path.
 */
static int log_async_vprintf(const char *fmt, va_list args)
{
    if (must_log_sync()) {
        return s_prev ? s_prev(fmt, args) : 0;
    }
    va_list len_args;
    va_copy(len_args, args);
    int n = vsnprintf(NULL, 0, fmt, len_args);
    va_end(len_args);
    void *slot = NULL;
    if (n <= 0 || xRingbufferSendAcquire(s_rb, &slot, (size_t)n + 1, 0) != pdTRUE) {
        if (n > 0) atomic_fetch_add(&s_dropped, 1);
        return n;
    }
    vsnprintf((char *)slot, (size_t)n + 1, fmt, args);
    xRingbufferSendComplete(s_rb, slot);
    return n;
}

/**
 * @brief Writer task: emit queued lines through the previous hook chain, and report drops.
 * v2: takes the ring handle as its task argument. It read s_rb, which log_async_init()
 *     publishes only after creating this task (so the hook stays synchronous until ready);
 *     the task ran first and asserted in xRingbufferReceive(NULL) at every boot.
 * @version 2
 * @req REQ-LOG-005
 * @callback
 */
static void log_async_writer(void *arg)
{
    RingbufHandle_t rb = (RingbufHandle_t)arg;
    uint32_t reported = 0;
    while (true) {
        size_t len = 0;
        char *line = (char *)xRingbufferReceive(rb, &len, portMAX_DELAY);
        if (line) {
            forward_prev("%s", line);   // NUL-terminated by the producer's vsnprintf
            vRingbufferReturnItem(rb, line);
        }
        uint32_t dropped = atomic_load(&s_dropped);
        if (dropped != reported) {
            ESP_LOGW(TAG, "%lu log line(s) dropped: ring buffer full (total %lu)",
                     (unsigned long)(dropped - reported), (unsigned long)dropped);
            reported = dropped;
        }
    }
}

/**
 * @brief Install the ring buffer + writer task in front of the current log hook chain.
 * v2: passes the ring handle to the writer task as its argument (startup race fix).
 * @version 2
 * @req REQ-LOG-005
 * @return ESP_OK, or ESP_ERR_NO_MEM (logging then stays synchronous).
 */
esp_err_t log_async_init(void)
{
    if (s_rb) return ESP_OK;
    RingbufHandle_t rb = xRingbufferCreateWithCaps(LOG_ASYNC_RING_BYTES, RINGBUF_TYPE_NOSPLIT,
                                                   MALLOC_CAP_SPIRAM);
    if (!rb) {
        ESP_LOGE(TAG, "ring buffer alloc failed (%d B) — logging stays synchronous",
                 LOG_ASYNC_RING_BYTES);
        return ESP_ERR_NO_MEM;
    }
    // Stack in internal RAM: the writer does SD (FATFS) I/O, which needs an internal stack.
    if (xTaskCreatePinnedToCoreWithCaps(log_async_writer, "log_async", LOG_ASYNC_TASK_STACK, rb,
                                        LOG_ASYNC_TASK_PRIO, &s_writer, tskNO_AFFINITY,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        vRingbufferDeleteWithCaps(rb);
        ESP_LOGE(TAG, "writer task create failed — logging stays synchronous");
        return ESP_ERR_NO_MEM;
    }
    s_prev = esp_log_set_vprintf(log_async_vprintf);
    s_rb = rb;   // publish last: until now every call took the synchronous path
    ESP_LOGI(TAG, "async logging on (%d B ring, writer prio %d)", LOG_ASYNC_RING_BYTES,
             LOG_ASYNC_TASK_PRIO);
    return ESP_OK;
}

/**
 * @brief Number of log lines dropped because the ring buffer was full (since boot).
 * @version 1
 * @req REQ-LOG-005
 * @return Dropped-line count.
 */
uint32_t log_async_dropped(void)
{
    return atomic_load(&s_dropped);
}
