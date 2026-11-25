/**
 * @file auth.c
 * v2: auth_pair_secs_left.
 * @version 2
 * @brief The clock's API access (see auth.h): auth_core under a spinlock, the hardware
 *        random number generator, the monotonic clock and the admin token.
 */
#include "auth.h"
#include "secrets.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "auth";

static auth_state_t s_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Offer a new pairing code.
 * @version 1
 * @req REQ-UI-005
 */
void auth_pair_begin(char code_out[AUTH_TOKEN_LEN])
{
    uint8_t rnd[AUTH_RANDOM_BYTES];
    esp_fill_random(rnd, sizeof(rnd));
    taskENTER_CRITICAL(&s_lock);
    auth_core_pair_begin(&s_state, esp_timer_get_time(), rnd);
    memcpy(code_out, s_state.code, AUTH_TOKEN_LEN);
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "pairing code offered (10 min)");
}

/**
 * @brief Withdraw the pairing code.
 * @version 1
 * @req REQ-UI-005
 */
void auth_pair_end(void)
{
    taskENTER_CRITICAL(&s_lock);
    auth_core_pair_end(&s_state);
    taskEXIT_CRITICAL(&s_lock);
}

/**
 * @brief Trade a code for a session.
 * @version 1
 * @req REQ-UI-005
 * @return Whether accepted.
 */
bool auth_pair(const char *code, char token_out[AUTH_TOKEN_LEN])
{
    uint8_t rnd[AUTH_RANDOM_BYTES];
    esp_fill_random(rnd, sizeof(rnd));
    taskENTER_CRITICAL(&s_lock);
    bool ok = auth_core_pair(&s_state, esp_timer_get_time(), code, rnd, token_out);
    taskEXIT_CRITICAL(&s_lock);
    if (ok) ESP_LOGI(TAG, "phone paired (session 30 min)");
    else ESP_LOGW(TAG, "pairing refused: wrong, used or expired code");
    return ok;
}

/**
 * @brief Whether a bearer token may make a request.
 * @version 1
 * @req REQ-UI-005
 * @return True when allowed.
 */
bool auth_allows(auth_level_t need, const char *bearer)
{
    bool ok = need == AUTH_PUBLIC || auth_core_equal(bearer, OTA_API_TOKEN);
    if (!ok && need == AUTH_SESSION) {
        taskENTER_CRITICAL(&s_lock);
        ok = auth_core_session_ok(&s_state, esp_timer_get_time(), bearer);
        taskEXIT_CRITICAL(&s_lock);
    }
    return ok;
}

/**
 * @brief Seconds the offered code has left.
 * @version 1
 * @req REQ-UI-005
 * @return Seconds, or 0.
 */
int auth_pair_secs_left(void)
{
    taskENTER_CRITICAL(&s_lock);
    int n = auth_core_code_secs_left(&s_state, esp_timer_get_time());
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

/**
 * @brief How many phones are paired.
 * @version 1
 * @req REQ-UI-005
 * @return The count.
 */
int auth_sessions(void)
{
    taskENTER_CRITICAL(&s_lock);
    int n = auth_core_sessions(&s_state, esp_timer_get_time());
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

/**
 * @brief Disconnect every phone.
 * @version 1
 * @req REQ-UI-005
 */
void auth_revoke_all(void)
{
    taskENTER_CRITICAL(&s_lock);
    auth_core_revoke_all(&s_state);
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "all phones disconnected");
}
