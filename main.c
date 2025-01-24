/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
/* authok light turn on nahi ...no cpurestart,no rbout_slow */

//#include <stdbool.h>
//#include "hal/cpu_hal.h"   // For cpu_hal_get_cycle_count
//#include "freertos/FreeRTOS.h"  // For xPortGetCoreID


#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_board_init.h"
#include "speech_commands_action.h"
#include "model_path.h"
#include "esp_process_sdkconfig.h"

//websocket client
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

//variables 
#define WIFI_SSID "Upeya-2-4"
#define WIFI_PASS "asdfghjkl"
#define CONFIG_WEBSOCKET_URI "ws://192.168.1.20:8123/api/websocket" // Update IP and endpoint 1
static TimerHandle_t shutdown_signal_timer;
static const char *TAG = "example_connect";
static esp_websocket_client_handle_t client = NULL;
static void websocket_app_start(void);
static void wifi_init(void);
//global declaration
char data[512];   //1.1
int i=0;   //1.2
const char *entity_id = "switch.6switch1fan_3_switch_5"; //3  1.3

#define NO_DATA_TIMEOUT_SEC 10
static SemaphoreHandle_t shutdown_sema;
static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}


/* current problem -jan 9 resolvved -premature turn on
lights on before wake word detection
in detect task after detect .. payload/ws code (first wwd then speech then ws then turn on light) should be kept
*/
//wakeword_speech_turnon()//ww detection-speech command-payload-turn on light


/*current problem - jan 10 
rb_slow error  resolved*/
static void websocket_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        // Send authentication message
            esp_websocket_client_send_text((esp_websocket_client_handle_t)arg,
                "{\"type\":\"auth\",\"access_token\":\"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJlNzA5ZWI2ZmVhZmQ0MjcyOGU4MGFiMDBiYzNmOWExMyIsImlhdCI6MTczNjQwMjUxNSwiZXhwIjoyMDUxNzYyNTE1fQ.3vJMvNgfrbgn7tC0Y55wKG168QTnYb3xn1ISUPxiyLs\"}",
                strlen("{\"type\":\"auth\",\"access_token\":\"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJlNzA5ZWI2ZmVhZmQ0MjcyOGU4MGFiMDBiYzNmOWExMyIsImlhdCI6MTczNjQwMjUxNSwiZXhwIjoyMDUxNzYyNTE1fQ.3vJMvNgfrbgn7tC0Y55wKG168QTnYb3xn1ISUPxiyLs\"}"), portMAX_DELAY);
        break;
    
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "Received WebSocket Data: %.*s", data->data_len, (char *)data->data_ptr);   //change-1
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x08 && data->data_len == 2) {
            ESP_LOGW(TAG, "Received closed message with code=%d", 512*data->data_ptr[0] + data->data_ptr[1]);  //256 thila
        } else {
            ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
        }
        ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
        //change-2
    default:
        ESP_LOGW(TAG, "Unknown WebSocket Event ID: %d", event_id);
        break;

    }
}


// Updated websocket_app_start
static void websocket_app_start(void) {
    esp_websocket_client_config_t websocket_cfg = {
        .uri = CONFIG_WEBSOCKET_URI,
    };

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    if (!shutdown_signal_timer) {
        ESP_LOGE(TAG, "Failed to create timer");
        return;
    }

    shutdown_sema = xSemaphoreCreateBinary();
    if (!shutdown_sema) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        xTimerDelete(shutdown_signal_timer, portMAX_DELAY);
        return;
    }

    client = esp_websocket_client_init(&websocket_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
    //    xSemaphoreDelete(shutdown_sema,portMAX_DELAY);
     //   xTimerDelete(shutdown_signal_timer, portMAX_DELAY);
        return;
    }

    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    ESP_LOGI(TAG, "Connecting to WebSocket...");
    if (esp_websocket_client_start(client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client");
        esp_websocket_client_destroy(client);
     //   xSemaphoreDelete(shutdown_sema,portMAX_DELAY);
      //  xTimerDelete(shutdown_signal_timer, portMAX_DELAY);
        return;
    }

    xTimerStart(shutdown_signal_timer, portMAX_DELAY);

    // Perform WebSocket operations...

    // Cleanup
    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
  //  xSemaphoreDelete(shutdown_sema,portMAX_DELAY);
  //  xTimerDelete(shutdown_signal_timer,portMAX_DELAY);
    ESP_LOGI(TAG, "WebSocket stopped");
}
/*
static void websocket_app_start(void)
{
    /*
    esp_websocket_client_config_t websocket_cfg = {};

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();


    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;*/
   // websocket_cfg.transport = WEBSOCKET_TRANSPORT_OVER_TCP;
/*
    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);*/

    //esp_websocket_client_start(client); jan10
    //xTimerStart(shutdown_signal_timer, portMAX_DELAY); jan10
/*jan 9*/
    // Send authentication message to Home Assistant // Step 1: Authenticate
   //char auth_message[] = "{\"type\":\"auth\",\"access_token\":\"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJlNzA5ZWI2ZmVhZmQ0MjcyOGU4MGFiMDBiYzNmOWExMyIsImlhdCI6MTczNjQwMjUxNSwiZXhwIjoyMDUxNzYyNTE1fQ.3vJMvNgfrbgn7tC0Y55wKG168QTnYb3xn1ISUPxiyLs\"}"; //2
    //ESP_LOGI(TAG, "Sending authentication message...");
 //  esp_websocket_client_send_text(client, auth_message, strlen(auth_message), portMAX_DELAY); //1*/
  /* vTaskDelay(2000 / portTICK_RATE_MS); 

 
    
 //char data[256];
    //int i=0;
    while(i<10)
    {
    if (esp_websocket_client_is_connected(client)){
 // const char *entity_id = "switch.6switch1fan_3_switch_5"; //3
 // snprintf(data, sizeof(data),
         //  "{\"id\":1 ,\"type\":\"call_service\",\"domain\":\"switch\",\"service\":\"turn_on\",\"target\":{\"entity_id\":[\"switch.6switch1fan_3_switch_5\"]}}");//4
  //  ESP_LOGI(TAG, "Sending %s", data);
           // esp_websocket_client_send_text(client, data, strlen(data) ,portMAX_DELAY);
        }
        vTaskDelay(5000 / portTICK_RATE_MS);

    }
    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    //esp_websocket_client_stop(client);
    //esp_websocket_client_destroy(client);
    esp_websocket_client_close(client, portMAX_DELAY);
    ESP_LOGI(TAG, "Websocket Stopped");
    esp_websocket_client_destroy(client);
}*/
/* Wi-Fi Initialization */

esp_err_t send_websocket_command(const char *command) {
    if (client == NULL) {
        ESP_LOGE(TAG, "WebSocket client is not initialized");
        return ESP_FAIL;
    }

    if (!esp_websocket_client_is_connected(client)) {
        ESP_LOGE(TAG, "WebSocket client is not connected");
        return ESP_FAIL;
    }

    // Send the command
    int bytes_sent = esp_websocket_client_send_text(client, command, strlen(command), portMAX_DELAY);
    if (bytes_sent > 0) {
        ESP_LOGI(TAG, "WebSocket command sent successfully: %s", command);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to send WebSocket command: %s", command);
        return ESP_FAIL;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying connection to Wi-Fi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected to Wi-Fi");
    }
}

static void wifi_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_any_id);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}






static int request_id=0;
//request_id++;
int detect_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
srmodel_list_t *models = NULL;
static int play_voice = -2;


void play_music(void *arg)
{
    while (task_flag) {
        switch (play_voice) {
        case -2:
            vTaskDelay(10);
            break;
        case -1:
            wake_up_action();
            play_voice = -2;
            break;
        default:
            speech_commands_action(play_voice);
            play_voice = -2;
            break;
        }
    }
    vTaskDelete(NULL);
}

void handle_turn_on_command() {
    const char *command = "{\"id\":1,\"type\":\"call_service\",\"domain\":\"switch\",\"service\":\"turn_on\",\"target\":{\"entity_id\":[\"switch.6switch1fan_3_switch_5\"]}}";
    esp_err_t result = send_websocket_command(command);
    if (result == ESP_OK) {
        printf("[PLAY_MUSIC] TURN ON command sent successfully.\n");
    } else {
        printf("[PLAY_MUSIC] Failed to send TURN ON command. Error: %d\n", result);
    }
  //  const char *command = "{\"id\":1,\"type\":\"call_service\",\"domain\":\"switch\",\"service\":\"turn_on\",\"target\":{\"entity_id\":[\"switch.6switch1fan_3_switch_5\"]}}";
if (send_websocket_command(command) == ESP_OK) {
    ESP_LOGI(TAG, "Command sent successfully");
} else {
    ESP_LOGE(TAG, "Failed to send command");
}
}
/*
void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch <= feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag) {
        esp_get_feed_data(false, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);

        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}*/
/*
void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch <= feed_channel);

    // Allocate buffer for initial audio chunk size
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);
    
    int current_chunksize = audio_chunksize;

    while (task_flag) {
        // Check if the chunk size has changed dynamically
        audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
        if (audio_chunksize != current_chunksize) {
            // Resize the buffer if chunk size changes
            int16_t *new_buff = realloc(i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
            if (!new_buff) {
                // Handle realloc failure
                free(i2s_buff);
                ESP_LOGE("feed_Task", "Buffer realloc failed");
                vTaskDelete(NULL);
                return;
            }
            i2s_buff = new_buff;
            current_chunksize = audio_chunksize;
        }

        // Get feed data and feed it to AFE
        esp_get_feed_data(false, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        afe_handle->feed(afe_data, i2s_buff);
    }

    // Cleanup resources
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}*/
// Updated feed_Task with proper memory management
void feed_Task(void *arg) {
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch <= feed_channel);

    // Allocate buffer for initial audio chunk size
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    if (!i2s_buff) {
        ESP_LOGE("feed_Task", "Memory allocation failed");
        vTaskDelete(NULL);
        return;
    }
    
    int current_chunksize = audio_chunksize;

    while (task_flag) {
        // Check if the chunk size has changed dynamically
        audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
        if (audio_chunksize != current_chunksize) {
            // Resize the buffer if chunk size changes
            int16_t *new_buff = realloc(i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
            if (!new_buff) {
                ESP_LOGE("feed_Task", "Buffer realloc failed");
                free(i2s_buff);
                vTaskDelete(NULL);
                return;
            }
            i2s_buff = new_buff;
            current_chunksize = audio_chunksize;
        }

        // Get feed data and feed it to AFE
        esp_get_feed_data(false, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        afe_handle->feed(afe_data, i2s_buff);
    }

    // Cleanup resources
    free(i2s_buff);
    i2s_buff = NULL;
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{

   // char data[256];
   // int i=0;
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    printf("multinet:%s\n", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    esp_mn_commands_update_from_sdkconfig(multinet, model_data); // Add speech commands from sdkconfig
    assert(mu_chunksize == afe_chunksize);
    //print active speech commands
    multinet->print_active_speech_commands(model_data);

    printf("------------detect start------------\n");
    while (task_flag) {
        afe_fetch_result_t* res = afe_handle->fetch(afe_data); 
        if (!res || res->ret_value == ESP_FAIL) {
            printf("fetch error!\n");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("WAKEWORD DETECTED\n");
	    multinet->clean(model_data);
        } else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            play_voice = -1;
            detect_flag = 1;
            printf("AFE_FETCH_CHANNEL_VERIFIED, channel index: %d\n", res->trigger_channel_id);
            // afe_handle->disable_wakenet(afe_data);
            // afe_handle->disable_aec(afe_data);
        }

        if (detect_flag == 1) {
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                for (int i = 0; i < mn_result->num; i++) {
                    printf("TOP %d, command_id: %d, phrase_id: %d, string: %s, prob: %f\n", 
                    i+1, mn_result->command_id[i], mn_result->phrase_id[i], mn_result->string, mn_result->prob[i]);
                     printf("Said by Pratik");
                     printf("Command is : %s",mn_result->string);
                      if(mn_result->command_id[i]==9)
                      {
                        //char data[256];
                      //  int i=0;
                      esp_websocket_client_config_t websocket_cfg = {};

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();


    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;
     ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

                       esp_websocket_client_start(client);
                      xTimerStart(shutdown_signal_timer, portMAX_DELAY);
                       // websocket_app_start();  //it shows error of invalid arguments when commented here and gets rb_slow error if uncommented here even without this premture turnonn error is resolved,data is printed
                       char auth_message[] = "{\"type\":\"auth\",\"access_token\":\"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJlNzA5ZWI2ZmVhZmQ0MjcyOGU4MGFiMDBiYzNmOWExMyIsImlhdCI6MTczNjQwMjUxNSwiZXhwIjoyMDUxNzYyNTE1fQ.3vJMvNgfrbgn7tC0Y55wKG168QTnYb3xn1ISUPxiyLs\"}"; //2
                       esp_websocket_client_send_text(client, auth_message, strlen(auth_message), portMAX_DELAY); //1
                      const char *entity_id = "switch.6switch1fan_3_switch_5"; //3
                  
   snprintf(data, sizeof(data),
            "{\"id\":1 ,\"type\":\"call_service\",\"domain\":\"switch\",\"service\":\"turn_on\",\"target\":{\"entity_id\":[\"switch.6switch1fan_3_switch_5\"]}}",++request_id,entity_id);//4
            esp_websocket_client_send_text(client, data, strlen(data), portMAX_DELAY); //1
            void handle_turn_on_command();
            
                         printf("turn on light"); 
                          printf("%s myyyy data...",data);

                       
  ESP_LOGI(TAG, "Sending authentication message...");
                      }
                      else
                      {
                         printf("not working");
                      }
                }
                printf("-----------listening-----------\n");
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("timeout, string:%s\n", mn_result->string);
                afe_handle->enable_wakenet(afe_data);
                detect_flag = 0;
                printf("\n-----------awaits to be waken up-----------\n");
                continue;
            }
        }
    }
    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    printf("detect exit\n");
    vTaskDelete(NULL);
}

void app_main()
{
//debug checks
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    //esp_log_level_set("*", ESP_LOG_INFO);
  //  esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_DEBUG);
   // esp_log_level_set("TRANSPORT_WS", ESP_LOG_DEBUG);
   // esp_log_level_set("TRANS_TCP", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

 

    models = esp_srmodel_init("model"); // partition label defined in partitions.csv
    ESP_ERROR_CHECK(esp_board_init(16000, 2, 16));
    // ESP_ERROR_CHECK(esp_sdcard_init("/sdcard", 10));

#if CONFIG_IDF_TARGET_ESP32
    printf("This demo only support ESP32S3\n");
    return;
#else 
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
#endif

    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);;
#if CONFIG_ESP32_S3_EYE_BOARD || CONFIG_ESP32_P4_FUNCTION_EV_BOARD
    afe_config.pcm_config.total_ch_num = 2;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 1;
    afe_config.wakenet_mode = DET_MODE_90;
    afe_config.se_init = false;
#endif
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(&detect_Task, "detect", 16 * 1024, (void*)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);

#if defined  CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD
    xTaskCreatePinnedToCore(&led_Task, "led", 3 * 1024, NULL, 5, NULL, 0);
#endif


    // // You can call afe_handle->destroy to destroy AFE.
    // task_flag = 0;

   // printf("destroy\n");
   //  afe_handle->destroy(afe_data);
   //  afe_data = NULL;
   //  printf("successful\n");
    ESP_ERROR_CHECK(example_connect());
    websocket_app_start();
   // wakeup_action();
}
