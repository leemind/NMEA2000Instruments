/*
 * pgn_json_parser.c - Implementation of JSON PGN parser using cJSON
 */

#include "pgn_json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "PGN_PARSER";
static char *pgn_db_buffer = NULL;
static size_t pgn_db_size = 0;
static SemaphoreHandle_t pgn_db_mutex = NULL;

// Cache for the last parsed PGN object
static int cached_pgn_number = -1;
static cJSON *cached_pgn_json = NULL;

static char *cached_pgn_id = NULL;
static cJSON *cached_id_json = NULL;

static void *cjson_psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

static void cjson_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

cJSON *pgn_json_load(const char *filepath)
{
    // Initialize cJSON to use PSRAM for all allocations
    static bool hooks_initialized = false;
    if (!hooks_initialized) {
        cJSON_Hooks hooks = {
            .malloc_fn = cjson_psram_malloc,
            .free_fn = cjson_psram_free
        };
        cJSON_InitHooks(&hooks);
        hooks_initialized = true;
    }

    if (!pgn_db_mutex) {
        pgn_db_mutex = xSemaphoreCreateMutex();
    }

    if (pgn_db_buffer) {
        free(pgn_db_buffer);
        pgn_db_buffer = NULL;
    }

    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "File size is 0 or negative: %ld", file_size);
        fclose(file);
        return NULL;
    }

    pgn_db_buffer = heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM);
    if (!pgn_db_buffer) {
        ESP_LOGE(TAG, "Memory allocation failed for database buffer (size %ld)", file_size);
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(pgn_db_buffer, 1, file_size, file);
    if (read_bytes != file_size) {
        ESP_LOGE(TAG, "Failed to read entire file (read %zu of %ld)", read_bytes, file_size);
        free(pgn_db_buffer);
        pgn_db_buffer = NULL;
        fclose(file);
        return NULL;
    }

    pgn_db_buffer[file_size] = '\0';
    pgn_db_size = file_size;
    fclose(file);

    ESP_LOGI(TAG, "Successfully loaded %ld bytes of PGN database into PSRAM string", file_size);
    
    // Return a dummy cJSON object so caller thinks it worked
    // We'll use the buffer for actual lookups
    return (cJSON*)pgn_db_buffer; 
}

cJSON *pgn_get_definition(cJSON *pgn_db, int pgn_number)
{
    char *buffer = (char*)pgn_db;
    if (!buffer) return NULL;

    if (xSemaphoreTake(pgn_db_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return NULL;
    }

    // Check cache
    if (pgn_number == cached_pgn_number && cached_pgn_json) {
        cJSON *res = cached_pgn_json;
        xSemaphoreGive(pgn_db_mutex);
        return res;
    }

    // Clear old cache
    if (cached_pgn_json) {
        cJSON_Delete(cached_pgn_json);
        cached_pgn_json = NULL;
        cached_pgn_number = -1;
    }

    // Search
    char search_str[32];
    snprintf(search_str, sizeof(search_str), "\"PGN\": %d", pgn_number);
    
    char *pos = strstr(buffer, search_str);
    if (!pos) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    char *start = pos;
    while (start > buffer && *start != '{') {
        start--;
    }

    if (*start != '{') {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cJSON *pgn_obj = cJSON_Parse(start);
    if (!pgn_obj) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cJSON *pgn_val = cJSON_GetObjectItem(pgn_obj, "PGN");
    if (!pgn_val || pgn_val->valueint != pgn_number) {
        cJSON_Delete(pgn_obj);
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cached_pgn_number = pgn_number;
    cached_pgn_json = pgn_obj;

    cJSON *res = cached_pgn_json;
    xSemaphoreGive(pgn_db_mutex);
    return res;
}

cJSON *pgn_get_definition_by_id(cJSON *pgn_db, const char *pgn_id)
{
    char *buffer = (char*)pgn_db;
    if (!buffer || !pgn_id) return NULL;

    if (xSemaphoreTake(pgn_db_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return NULL;
    }

    // Check cache
    if (cached_pgn_id && strcmp(cached_pgn_id, pgn_id) == 0 && cached_id_json) {
        cJSON *res = cached_id_json;
        xSemaphoreGive(pgn_db_mutex);
        return res;
    }

    // Clear old cache
    if (cached_id_json) {
        cJSON_Delete(cached_id_json);
        cached_id_json = NULL;
    }
    if (cached_pgn_id) {
        free(cached_pgn_id);
        cached_pgn_id = NULL;
    }

    // Search for "Id": "pgn_id"
    char search_str[128];
    snprintf(search_str, sizeof(search_str), "\"Id\": \"%s\"", pgn_id);
    
    char *pos = strstr(buffer, search_str);
    if (!pos) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    // Find the start of the object {
    char *start = pos;
    while (start > buffer && *start != '{') {
        start--;
    }
    if (*start != '{') {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cJSON *pgn_obj = cJSON_Parse(start);
    if (!pgn_obj) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cached_id_json = pgn_obj;
    cached_pgn_id = strdup(pgn_id);

    cJSON *res = cached_id_json;
    xSemaphoreGive(pgn_db_mutex);
    return res;
}

void pgn_print_all_ids(cJSON *pgn_db)
{
    if (xSemaphoreTake(pgn_db_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    ESP_LOGI(TAG, "On-demand parsing active. Skipping full ID print to save memory.");
    xSemaphoreGive(pgn_db_mutex);
}

void pgn_parse_systemtime(cJSON *pgn_def)
{
    if (!pgn_def) {
        ESP_LOGE(TAG, "Invalid PGN definition");
        return;
    }

    cJSON *pgn_num = cJSON_GetObjectItem(pgn_def, "PGN");
    cJSON *id = cJSON_GetObjectItem(pgn_def, "Id");
    cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");

    ESP_LOGI(TAG, "PGN: %d (%s)", pgn_num->valueint, id->valuestring);
    ESP_LOGI(TAG, "Fields:");

    cJSON *field = NULL;
    cJSON_ArrayForEach(field, fields) {
        cJSON *order = cJSON_GetObjectItem(field, "Order");
        cJSON *field_id = cJSON_GetObjectItem(field, "Id");
        cJSON *name = cJSON_GetObjectItem(field, "Name");
        cJSON *bit_length = cJSON_GetObjectItem(field, "BitLength");
        cJSON *resolution = cJSON_GetObjectItem(field, "Resolution");

        ESP_LOGI(TAG, "  [%d] %s (%s) - %d bits @ res %.4f",
                 order->valueint,
                 field_id->valuestring,
                 name->valuestring,
                 bit_length->valueint,
                 resolution->valuedouble);
    }
}
