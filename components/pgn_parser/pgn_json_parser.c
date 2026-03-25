/*
 * pgn_json_parser.c - Implementation of JSON PGN parser using cJSON
 */
#include "pgn_json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <esp_log.h>
#include "esp_heap_caps.h"

static const char *TAG = "PGN_PARSER";

cJSON *pgn_json_load(const char *filepath)
{
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    ESP_LOGI(TAG, "Loading %ld bytes from %s", file_size, filepath);
    ESP_LOGI(TAG, "Free PSRAM: %u, Internal: %u", 
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Force allocation in PSRAM for large JSON buffers
    char *buffer = heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "PSRAM allocation failed for %ld bytes", file_size + 1);
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, file_size, file);
    buffer[read_bytes] = '\0';
    fclose(file);

    if (read_bytes != file_size) {
        ESP_LOGW(TAG, "Read mismatch: expected %ld, got %zu", file_size, read_bytes);
    }

    cJSON *json = cJSON_Parse(buffer);
    
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON. Error near: %s", cJSON_GetErrorPtr());
        // Diagnostic: log first 32 bytes to check for corruption
        ESP_LOG_BUFFER_HEX(TAG, buffer, (read_bytes > 32 ? 32 : read_bytes));
    }

    free(buffer);

    if (!json) return NULL;

    ESP_LOGI(TAG, "Successfully loaded PGN database. Remaining PSRAM: %u", 
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return json;
}

cJSON *pgn_get_definition(cJSON *pgn_db, int pgn_number)
{
    if (!pgn_db) return NULL;
    cJSON *pgns = cJSON_GetObjectItem(pgn_db, "pgns");
    if (!pgns || !pgns->child) return NULL;
    cJSON *pgn = NULL;
    cJSON_ArrayForEach(pgn, pgns) {
        cJSON *pgn_num = cJSON_GetObjectItem(pgn, "PGN");
        if (pgn_num && pgn_num->valueint == pgn_number) {
            return pgn;
        }
    }
    return NULL;
}

cJSON *pgn_get_definition_by_id(cJSON *pgn_db, const char *pgn_id)
{
    if (!pgn_db || !pgn_id) return NULL;
    cJSON *pgns = cJSON_GetObjectItem(pgn_db, "pgns");
    if (!pgns || !pgns->child) return NULL;
    cJSON *pgn = NULL;
    cJSON_ArrayForEach(pgn, pgns) {
        cJSON *id = cJSON_GetObjectItem(pgn, "Id");
        if (id && id->valuestring && strcmp(id->valuestring, pgn_id) == 0) {
            return pgn;
        }
    }
    return NULL;
}

void pgn_print_all_ids(cJSON *pgn_db)
{
    if (!pgn_db) return;
    cJSON *pgns = cJSON_GetObjectItem(pgn_db, "pgns");
    if (!pgns || !pgns->child) return;
    int count = 0;
    cJSON *pgn = NULL;
    cJSON_ArrayForEach(pgn, pgns) {
        cJSON *pgn_num = cJSON_GetObjectItem(pgn, "PGN");
        cJSON *id = cJSON_GetObjectItem(pgn, "Id");
        cJSON *desc = cJSON_GetObjectItem(pgn, "Description");
        if (pgn_num && id) {
            ESP_LOGI(TAG, "[%4d] PGN: %d | Id: %s | %s", 
                     count++, pgn_num->valueint,
                     id->valuestring ? id->valuestring : "N/A",
                     desc && desc->valuestring ? desc->valuestring : "N/A");
        }
    }
    ESP_LOGI(TAG, "Total PGNs: %d", count);
}
