/*
 * pgn_json_parser.c - Implementation of JSON PGN parser using cJSON
 */

#include "pgn_json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <esp_log.h>

static const char *TAG = "PGN_PARSER";

cJSON *pgn_json_load(const char *filepath)
{
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return NULL;
    }

    // Read entire file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Memory allocation failed");
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, file_size, file) != file_size) {
        ESP_LOGE(TAG, "Failed to read file");
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[file_size] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(buffer);
    free(buffer);

    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return NULL;
    }

    ESP_LOGI(TAG, "Successfully loaded PGN database from %s", filepath);
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
    if (!pgn_db) {
        ESP_LOGE(TAG, "Invalid PGN database");
        return;
    }

    cJSON *pgns = cJSON_GetObjectItem(pgn_db, "pgns");
    if (!pgns || !pgns->child) {
        ESP_LOGE(TAG, "No PGNs found in database");
        return;
    }

    int count = 0;
    cJSON *pgn = NULL;
    cJSON_ArrayForEach(pgn, pgns) {
        cJSON *pgn_num = cJSON_GetObjectItem(pgn, "PGN");
        cJSON *id = cJSON_GetObjectItem(pgn, "Id");
        cJSON *desc = cJSON_GetObjectItem(pgn, "Description");

        if (pgn_num && id) {
            ESP_LOGI(TAG, "[%4d] PGN: %d | Id: %s | %s", 
                     count++,
                     pgn_num->valueint,
                     id->valuestring ? id->valuestring : "N/A",
                     desc && desc->valuestring ? desc->valuestring : "N/A");
        }
    }

    ESP_LOGI(TAG, "Total PGNs: %d", count);
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
