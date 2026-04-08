#include "mqtt_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "command_router.h"
#include "ethernet_service.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "publish_queue.h"
#include "topic_map.h"

static const char *TAG = "mqtt_service";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static char s_online_payload[APP_JSON_PAYLOAD_MAX_LEN];
static char s_offline_payload[APP_JSON_PAYLOAD_MAX_LEN];
static char s_broker_uri[APP_TOPIC_MAX_LEN];

static esp_err_t build_presence_payloads(void)
{
    int written;

    written = snprintf(
        s_online_payload,
        sizeof(s_online_payload),
        "{\"state\":\"online\",\"node_id\":\"%s\"}",
        topic_map_get_node_id());
    if (written < 0 || (size_t)written >= sizeof(s_online_payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    written = snprintf(
        s_offline_payload,
        sizeof(s_offline_payload),
        "{\"state\":\"offline\",\"node_id\":\"%s\"}",
        topic_map_get_node_id());
    if (written < 0 || (size_t)written >= sizeof(s_offline_payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t mqtt_publish_presence_online(esp_mqtt_client_handle_t client)
{
    int msg_id = esp_mqtt_client_publish(
        client,
        topic_map_get_status_online_topic(),
        s_online_payload,
        0,
        1,
        true);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "failed to publish online presence");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "published online presence, msg_id=%d", msg_id);
    return ESP_OK;
}

static esp_err_t build_broker_uri_from_gateway(void)
{
    char gateway_ip[16];
    int written;

    ESP_RETURN_ON_ERROR(ethernet_service_get_gateway_string(gateway_ip, sizeof(gateway_ip)), TAG, "failed to resolve DHCP gateway");

    written = snprintf(s_broker_uri, sizeof(s_broker_uri), "mqtt://%s:%d", gateway_ip, APP_MQTT_BROKER_PORT);
    if (written < 0 || (size_t)written >= sizeof(s_broker_uri)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "using DHCP gateway as MQTT broker: %s", s_broker_uri);
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        mqtt_publish_presence_online(client);
        esp_mqtt_client_subscribe(client, topic_map_get_command_subscription_topic(), 1);
        esp_mqtt_client_subscribe(client, topic_map_get_broadcast_subscription_topic(), 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data on topic %.*s", event->topic_len, event->topic);
        command_router_handle(event->topic, event->topic_len, event->data, event->data_len);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error event");
        if (event->error_handle != NULL) {
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGW(TAG, "esp-tls err=0x%x, tls stack err=0x%x, errno=%d",
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_tls_stack_err,
                         event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGW(TAG, "connection refused, return code=0x%x", event->error_handle->connect_return_code);
            }
        }
        break;

    default:
        ESP_LOGD(TAG, "MQTT event id=%" PRIi32, event_id);
        break;
    }
}

esp_err_t mqtt_service_init(void)
{
    if (s_client != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(build_presence_payloads(), TAG, "failed to build presence payloads");
    ESP_RETURN_ON_ERROR(build_broker_uri_from_gateway(), TAG, "failed to build broker URI");

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_broker_uri,
        .network.disable_auto_reconnect = false,
        .credentials.client_id = topic_map_get_node_id(),
        .session.last_will.topic = topic_map_get_status_online_topic(),
        .session.last_will.msg = s_offline_payload,
        .session.last_will.msg_len = strlen(s_offline_payload),
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
        .session.keepalive = 30,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "failed to create MQTT client");

    return esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
}

esp_err_t mqtt_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_INVALID_STATE, TAG, "MQTT client not initialized");
    return esp_mqtt_client_start(s_client);
}

bool mqtt_service_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_service_publish_json(const char *topic, const char *json, int qos, bool retain)
{
    return mqtt_service_publish_bytes(topic, json, json != NULL ? strlen(json) : 0, qos, retain);
}

esp_err_t mqtt_service_publish_bytes(const char *topic, const void *data, size_t data_len, int qos, bool retain)
{
    return publish_queue_push(topic, data, data_len, qos, retain);
}

esp_err_t mqtt_service_publish_immediate_json(const char *topic, const char *json, int qos, bool retain)
{
    return mqtt_service_publish_immediate_bytes(topic, json, json != NULL ? strlen(json) : 0, qos, retain);
}

esp_err_t mqtt_service_publish_immediate_bytes(const char *topic, const void *data, size_t data_len, int qos, bool retain)
{
    int msg_id;

    ESP_RETURN_ON_FALSE(topic != NULL, ESP_ERR_INVALID_ARG, TAG, "topic is required");
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_INVALID_STATE, TAG, "MQTT client not initialized");

    if (!s_connected) {
        ESP_LOGD(TAG, "dropping publish while MQTT is disconnected: %s", topic);
        return ESP_ERR_INVALID_STATE;
    }

    msg_id = esp_mqtt_client_publish(s_client, topic, data, (int)data_len, qos, retain);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "failed to publish to %s", topic);
        return ESP_FAIL;
    }

    return ESP_OK;
}
