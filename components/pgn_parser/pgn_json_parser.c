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
#include "freertos/task.h"

static const char *TAG = "PGN_PARSER";
static char *pgn_db_path = NULL;
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

    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return NULL;
    }
    fclose(file);

    if (pgn_db_path) {
        free(pgn_db_path);
    }
    pgn_db_path = strdup(filepath);

    ESP_LOGI(TAG, "PGN database path set: %s (On-demand streaming enabled)", filepath);
    
    // Return a dummy non-NULL pointer to satisfy the caller
    return (cJSON*)pgn_db_path; 
}

static char *find_json_object_in_file(const char *filepath, const char *search_pattern)
{
    FILE *file = fopen(filepath, "r");
    if (!file) return NULL;

    const size_t chunk_size = 4096;
    const size_t overlap = 128; // To handle patterns split across chunks
    char *chunk = heap_caps_malloc(chunk_size + 1, MALLOC_CAP_SPIRAM);
    if (!chunk) {
        fclose(file);
        return NULL;
    }

    long found_offset = -1;
    size_t bytes_read;
    long current_offset = 0;

    while ((bytes_read = fread(chunk, 1, chunk_size, file)) > 0) {
        chunk[bytes_read] = '\0';
        char *p = strstr(chunk, search_pattern);
        if (p) {
            found_offset = current_offset + (p - chunk);
            break;
        }
        
        // Move back slightly for overlap, unless we reached EOF
        if (bytes_read == chunk_size) {
            fseek(file, -overlap, SEEK_CUR);
            current_offset += (chunk_size - (long)overlap);
        } else {
            current_offset += (long)bytes_read;
        }
    }

    if (found_offset == -1) {
        heap_caps_free(chunk);
        fclose(file);
        return NULL;
    }

    // Found the pattern. Now find the enclosing { }
    // Backtrack to find '{'
    fseek(file, found_offset, SEEK_SET);
    int c;
    long start_pos = found_offset;
    while (start_pos > 0) {
        fseek(file, --start_pos, SEEK_SET);
        c = fgetc(file);
        if (c == '{') break;
    }

    // Now read forward to find matching '}'
    fseek(file, start_pos, SEEK_SET);
    int brace_depth = 0;
    bool in_string = false;
    long end_pos = start_pos;
    
    while ((c = fgetc(file)) != EOF) {
        end_pos++;
        if (c == '"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (c == '{') brace_depth++;
            else if (c == '}') {
                brace_depth--;
                if (brace_depth == 0) break;
            }
        }
        if (end_pos - start_pos > 16384) break; // Safety limit
    }

    size_t obj_size = end_pos - start_pos;
    char *obj_buf = heap_caps_malloc(obj_size + 1, MALLOC_CAP_SPIRAM);
    if (obj_buf) {
        fseek(file, start_pos, SEEK_SET);
        fread(obj_buf, 1, obj_size, file);
        obj_buf[obj_size] = '\0';
    }

    heap_caps_free(chunk);
    fclose(file);
    return obj_buf;
}

cJSON *pgn_get_definition(cJSON *pgn_db, int pgn_number)
{
    if (!pgn_db_path) return NULL;

    if (xSemaphoreTake(pgn_db_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
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

    // Search in file
    char search_str[32];
    snprintf(search_str, sizeof(search_str), "\"PGN\": %d", pgn_number);
    
    char *json_str = find_json_object_in_file(pgn_db_path, search_str);
    if (!json_str) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cJSON *pgn_obj = cJSON_Parse(json_str);
    heap_caps_free(json_str);

    if (!pgn_obj) {
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
    if (!pgn_db_path || !pgn_id) return NULL;

    if (xSemaphoreTake(pgn_db_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
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
    
    char *json_str = find_json_object_in_file(pgn_db_path, search_str);
    if (!json_str) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    cJSON *pgn_obj = cJSON_Parse(json_str);
    heap_caps_free(json_str);

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
    ESP_LOGI(TAG, "On-demand parsing active. Skipping full ID print to save memory.");
}

void pgn_parse_systemtime(cJSON *pgn_def)
{
    if (!pgn_def) return;

    cJSON *pgn_num = cJSON_GetObjectItem(pgn_def, "PGN");
    cJSON *id = cJSON_GetObjectItem(pgn_def, "Id");
    cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");

    if (pgn_num && id) {
        ESP_LOGI(TAG, "PGN: %d (%s)", pgn_num->valueint, id->valuestring);
    }

    if (fields) {
        ESP_LOGI(TAG, "Fields:");
        cJSON *field = NULL;
        cJSON_ArrayForEach(field, fields) {
            cJSON *order = cJSON_GetObjectItem(field, "Order");
            cJSON *field_id = cJSON_GetObjectItem(field, "Id");
            cJSON *name = cJSON_GetObjectItem(field, "Name");
            cJSON *bit_length = cJSON_GetObjectItem(field, "BitLength");
            cJSON *resolution = cJSON_GetObjectItem(field, "Resolution");

            if (order && field_id && name && bit_length && resolution) {
                ESP_LOGI(TAG, "  [%d] %s (%s) - %d bits @ res %.4f",
                         order->valueint,
                         field_id->valuestring,
                         name->valuestring,
                         bit_length->valueint,
                         resolution->valuedouble);
            }
        }
    }
}

int pgn_search_by_description(const char *query, pgn_search_result_t *results, int max_results) {
    if (!pgn_db_path || !query || strlen(query) < 2) return 0;
    
    FILE *file = fopen(pgn_db_path, "r");
    if (!file) return 0;

    char *line = malloc(1024);
    if (!line) {
        fclose(file);
        return 0;
    }

    int count = 0;
    int current_pgn = 0;
    
    /* We assume standard formatting where PGN and Description are nearby.
       For a more robust search, we use a simple state machine to find PGN and match Description. */
    int yield_cnt = 0;
    while (fgets(line, 1024, file) && count < max_results) {
        if (++yield_cnt % 100 == 0) vTaskDelay(1); // Yield every 100 lines to prevent WDT trip
        char *p_pgn = strstr(line, "\"PGN\":");
        if (p_pgn) {
            current_pgn = atoi(p_pgn + 6);
        }

        char *p_desc = strstr(line, "\"Description\":");
        if (p_desc) {
            char *start = strchr(p_desc + 14, '\"');
            if (start) {
                start++;
                char *end = strchr(start, '\"');
                if (end) {
                    size_t len = end - start;
                    char desc_val[128];
                    if (len > 127) len = 127;
                    strncpy(desc_val, start, len);
                    desc_val[len] = '\0';

                    if (strcasestr(desc_val, query)) {
                        results[count].pgn = current_pgn;
                        strncpy(results[count].description, desc_val, 127);
                        results[count].description[127] = '\0';
                        count++;
                    }
                }
            }
        }
    }

    free(line);
    fclose(file);
    return count;
}
