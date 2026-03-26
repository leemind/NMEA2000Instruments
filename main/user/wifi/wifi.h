#ifndef _WIFI_H_
#define _WIFI_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_ERROR
} wifi_status_t;

/**
 * @brief Initialize WiFi and start connection task.
 */
void wifi_init(void);

/**
 * @brief Trigger a connection attempt with new credentials.
 */
void wifi_connect(const char *ssid, const char *pass);

/**
 * @brief Get current WiFi status.
 */
wifi_status_t wifi_get_status(void);

/**
 * @brief Get human-readable status string.
 */
const char* wifi_get_status_str(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_H_ */
