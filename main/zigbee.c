/* Zigbee end-device transport (see zigbee.h). Built on esp-zigbee-sdk. */
#include "zigbee.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"

static const char *TAG = "zigbee";

#define ESPINK_ENDPOINT      10
#define ESPINK_CLUSTER_ID    0xFC00
#define COALESCE_US          (2 * 1000 * 1000)   /* 2 s debounce */
#define ESPINK_ZB_STACK      6144

/* Slot attribute IDs and ZCL types, mirroring main/slots.h + the Z2M converter. */
typedef struct {
    uint16_t id;
    uint8_t  type;
} zb_attr_def_t;

static const zb_attr_def_t k_zb_attrs[] = {
    {ESPINK_ATTR_HDR_LEFT,     ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_HDR_RIGHT,    ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_NOW_VALUE,    ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_NOW_LABEL,    ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_NOW_HILO,     ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_FC1,          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_FC2,          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_FC3,          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_CAL1,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_CAL2,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_CAL3,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_CAL4,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SNS1,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SNS2,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SNS3,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SNS4,         ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_ALERT_TEXT,   ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_ALERT_ACTIVE, ESP_ZB_ZCL_ATTR_TYPE_BOOL},
    {ESPINK_ATTR_HEADACHE,     ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SHOP1,        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SHOP2,        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SHOP3,        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SHOP4,        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SHOP5,        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
    {ESPINK_ATTR_SHOP6,        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING},
};
#define N_ZB_ATTRS (sizeof(k_zb_attrs) / sizeof(k_zb_attrs[0]))

/* ZCL attribute storage. Strings are [len][bytes]; the bool uses byte 0. */
static uint8_t s_attr_buf[N_ZB_ATTRS][1 + ESPINK_SLOT_MAXLEN];

static espink_slots_t s_slots;
static esp_timer_handle_t s_coalesce_timer;
static espink_repaint_cb_t s_cb;
static void *s_ctx;

/* --- coalescing --- */

static void coalesce_fire(void *arg)
{
    (void)arg;
    if (s_cb) {
        s_cb(&s_slots, s_ctx);
    }
}

static void arm_coalesce(void)
{
    esp_timer_stop(s_coalesce_timer); /* ignore error if not running */
    ESP_ERROR_CHECK(esp_timer_start_once(s_coalesce_timer, COALESCE_US));
}

/* --- inbound attribute writes --- */

static void handle_attr_write(uint16_t attr_id, const esp_zb_zcl_attribute_data_t *data)
{
    char c[ESPINK_SLOT_MAXLEN + 1];

    if (data->type == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING && data->value) {
        const uint8_t *zs = (const uint8_t *)data->value;
        uint8_t len = zs[0];
        if (len > ESPINK_SLOT_MAXLEN) {
            len = ESPINK_SLOT_MAXLEN;
        }
        memcpy(c, zs + 1, len);
        c[len] = '\0';
    } else if (data->type == ESP_ZB_ZCL_ATTR_TYPE_BOOL && data->value) {
        c[0] = (*(const uint8_t *)data->value) ? '1' : '0';
        c[1] = '\0';
    } else {
        return;
    }

    if (espink_slots_set_attr(&s_slots, attr_id, c)) {
        ESP_LOGI(TAG, "slot 0x%04x updated -> '%s'", attr_id, c);
        arm_coalesce();
    }
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id, const void *msg)
{
    switch (cb_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
        const esp_zb_zcl_set_attr_value_message_t *m = msg;
        if (m->info.cluster == ESPINK_CLUSTER_ID) {
            handle_attr_write(m->attribute.id, &m->attribute.data);
        }
        break;
    }
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID: {
        const esp_zb_zcl_custom_cluster_command_message_t *m = msg;
        if (m->info.cluster == ESPINK_CLUSTER_ID) {
            ESP_LOGI(TAG, "render command -> immediate repaint");
            esp_timer_stop(s_coalesce_timer);
            coalesce_fire(NULL);
        }
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

/* --- endpoint construction --- */

/* Build a ZCL char string ([len][bytes]) from a C string into dst (>= 1+len). */
static void zcl_string(uint8_t *dst, const char *src)
{
    size_t n = strlen(src);
    if (n > ESPINK_SLOT_MAXLEN) {
        n = ESPINK_SLOT_MAXLEN;
    }
    dst[0] = (uint8_t)n;
    memcpy(dst + 1, src, n);
}

static esp_zb_cluster_list_t *build_clusters(void)
{
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x01, /* mains (Phase 1); battery in Phase 2 */
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    uint8_t manuf[1 + 16];
    uint8_t model[1 + 16];
    zcl_string(manuf, "ESPINK");
    zcl_string(model, "ESPINK-7in5b");
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manuf);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model);

    esp_zb_identify_cluster_cfg_t identify_cfg = {.identify_time = 0};
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&identify_cfg);

    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(ESPINK_CLUSTER_ID);
    for (size_t i = 0; i < N_ZB_ATTRS; i++) {
        s_attr_buf[i][0] = 0; /* empty string / false */
        esp_zb_custom_cluster_add_custom_attr(custom, k_zb_attrs[i].id, k_zb_attrs[i].type,
                                              ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
                                              s_attr_buf[i]);
    }

    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_custom_cluster(cl, custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    return cl;
}

/* --- stack lifecycle --- */

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "stack started, begin commissioning");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "not joined; starting network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "rejoined existing network");
            }
        } else {
            ESP_LOGW(TAG, "commissioning init failed (%s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "joined network successfully");
        } else {
            ESP_LOGW(TAG, "steering failed; retrying in 1s");
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGD(TAG, "signal 0x%x status %s", sig_type, esp_err_to_name(err_status));
        break;
    }
}

static void zb_task(void *arg)
{
    (void)arg;
    esp_zb_platform_config_t platform = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));

    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&zb_cfg);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = ESPINK_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, build_clusters(), ep_cfg);
    esp_zb_device_register(ep_list);

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void espink_zigbee_start(espink_repaint_cb_t repaint_cb, void *ctx)
{
    s_cb = repaint_cb;
    s_ctx = ctx;
    espink_slots_init(&s_slots);

    const esp_timer_create_args_t targs = {
        .callback = coalesce_fire,
        .name = "espink_coalesce",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_coalesce_timer));

    xTaskCreate(zb_task, "espink_zb", ESPINK_ZB_STACK, NULL, 5, NULL);
}
