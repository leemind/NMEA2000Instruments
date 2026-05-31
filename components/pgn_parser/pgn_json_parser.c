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

// The full 408 KB DB does not fit in one contiguous PSRAM block (fragmented by
// LVGL framebuffers). Instead we build a small in-RAM index mapping each PGN
// number to the byte offset of its '{' in the file, keep the file open, and
// seek straight to the object on lookup. This avoids the old per-call
// from-start re-scan (which cost ~171 ms) while using only a few KB of RAM.
typedef struct {
    int   pgn;
    long  offset; // byte offset of the object's opening '{'
    cJSON *def;   // lazily-parsed definition, cached after first lookup (NULL until then)
} pgn_index_entry_t;

static pgn_index_entry_t *pgn_index = NULL;
static int pgn_index_count = 0;
static int pgn_index_cap   = 0;
static FILE *pgn_db_file   = NULL; // kept open; accessed only under pgn_db_mutex

// Lazy per-PGN definition cache. Each distinct PGN is parsed once on first
// sight and kept (cJSON uses many small allocations, which fit fine in
// fragmented PSRAM). Real applications use far fewer than the cap; beyond it we
// stop caching and reuse a single scratch slot to avoid unbounded growth.
#define PGN_DEF_CACHE_CAP 48
static int   cached_def_count   = 0;     // number of index entries with def != NULL
static cJSON *uncached_scratch  = NULL;   // holds the last over-cap parse (freed on next over-cap miss)

// Single-entry cache for the rare by-id path (example/UI use only).
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

    // Close any previously-open handle / index (reload support).
    if (pgn_db_file) {
        fclose(pgn_db_file);
        pgn_db_file = NULL;
    }
    if (pgn_index) {
        for (int i = 0; i < pgn_index_count; i++) {
            if (pgn_index[i].def) cJSON_Delete(pgn_index[i].def);
        }
        heap_caps_free(pgn_index);
        pgn_index = NULL;
    }
    if (uncached_scratch) {
        cJSON_Delete(uncached_scratch);
        uncached_scratch = NULL;
    }
    pgn_index_count = 0;
    pgn_index_cap = 0;
    cached_def_count = 0;

    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return NULL;
    }

    // Build the PGN -> object-offset index in a single sequential pass.
    // The DB is pretty-printed (one field per line); the line whose first
    // non-space char is '{' opens an object, and "PGN" is its first field, so
    // the most recent '{' line before a "PGN" line is that object's start.
    // We track byte offsets by summing line lengths to avoid relying on ftell.
    pgn_index_cap = 512;
    pgn_index = heap_caps_malloc(pgn_index_cap * sizeof(pgn_index_entry_t),
                                 MALLOC_CAP_SPIRAM);
    char *line = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (!pgn_index || !line) {
        ESP_LOGE(TAG, "Failed to allocate PGN index");
        if (pgn_index) { heap_caps_free(pgn_index); pgn_index = NULL; }
        if (line) heap_caps_free(line);
        pgn_index_cap = 0;
        fclose(file);
        return NULL;
    }

    long offset = 0;
    long last_brace = -1;
    while (fgets(line, 1024, file)) {
        size_t llen = strlen(line);
        const char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '{') last_brace = offset;

        char *p = strstr(line, "\"PGN\":");
        if (p && last_brace >= 0) {
            if (pgn_index_count == pgn_index_cap) {
                int new_cap = pgn_index_cap * 2;
                pgn_index_entry_t *ni = heap_caps_realloc(
                    pgn_index, new_cap * sizeof(pgn_index_entry_t),
                    MALLOC_CAP_SPIRAM);
                if (!ni) break; // keep what we have
                pgn_index = ni;
                pgn_index_cap = new_cap;
            }
            pgn_index[pgn_index_count].pgn = atoi(p + 6);
            pgn_index[pgn_index_count].offset = last_brace;
            pgn_index[pgn_index_count].def = NULL;
            pgn_index_count++;
        }
        offset += (long)llen;
    }
    heap_caps_free(line);

    if (pgn_index_count == 0) {
        ESP_LOGE(TAG, "No PGN entries found in %s", filepath);
        heap_caps_free(pgn_index);
        pgn_index = NULL;
        pgn_index_cap = 0;
        fclose(file);
        return NULL;
    }

    // Keep the file open for fast seeks during lookups.
    pgn_db_file = file;

    if (pgn_db_path) {
        free(pgn_db_path);
    }
    pgn_db_path = strdup(filepath);

    ESP_LOGI(TAG, "PGN index built: %d entries from %s", pgn_index_count, filepath);

    // Return a dummy non-NULL pointer to satisfy the caller
    return (cJSON*)pgn_db_path;
}

// Read the JSON object that begins at brace_offset (the position of its '{')
// and return a heap_caps-allocated copy. Caller must hold pgn_db_mutex.
static char *read_object_at_offset(long brace_offset)
{
    if (!pgn_db_file || brace_offset < 0) return NULL;
    if (fseek(pgn_db_file, brace_offset, SEEK_SET) != 0) return NULL;

    size_t cap = 2048;
    size_t len = 0;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!out) return NULL;

    int brace_depth = 0;
    bool in_string = false;
    bool done = false;
    int c;
    while ((c = fgetc(pgn_db_file)) != EOF) {
        if (len + 1 >= cap) {
            if (cap >= 16384) break; // Safety limit
            size_t new_cap = cap * 2;
            char *nb = heap_caps_realloc(out, new_cap, MALLOC_CAP_SPIRAM);
            if (!nb) { heap_caps_free(out); return NULL; }
            out = nb;
            cap = new_cap;
        }
        out[len++] = (char)c;
        if (c == '"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (c == '{') brace_depth++;
            else if (c == '}') {
                brace_depth--;
                if (brace_depth == 0) { done = true; break; }
            }
        }
    }

    if (!done) { heap_caps_free(out); return NULL; }
    out[len] = '\0';
    return out;
}

// Scan the open DB file from the start for search_pattern and return the byte
// offset of the first match (or -1). Used only by the rare by-id path.
// Caller must hold pgn_db_mutex.
static long find_pattern_offset(const char *search_pattern)
{
    if (!pgn_db_file) return -1;
    if (fseek(pgn_db_file, 0, SEEK_SET) != 0) return -1;

    const size_t chunk_size = 4096;
    const size_t overlap = 128; // handle patterns split across chunks
    char *chunk = heap_caps_malloc(chunk_size + 1, MALLOC_CAP_SPIRAM);
    if (!chunk) return -1;

    long found = -1;
    long base = 0;
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, chunk_size, pgn_db_file)) > 0) {
        chunk[bytes_read] = '\0';
        char *p = strstr(chunk, search_pattern);
        if (p) { found = base + (p - chunk); break; }
        if (bytes_read == chunk_size) {
            fseek(pgn_db_file, -(long)overlap, SEEK_CUR);
            base += (long)(chunk_size - overlap);
        } else {
            base += (long)bytes_read;
        }
    }
    heap_caps_free(chunk);
    return found;
}

cJSON *pgn_get_definition(cJSON *pgn_db, int pgn_number)
{
    if (!pgn_db_path) return NULL;

    if (xSemaphoreTake(pgn_db_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return NULL;
    }

    // Find this PGN's index entry.
    pgn_index_entry_t *entry = NULL;
    for (int i = 0; i < pgn_index_count; i++) {
        if (pgn_index[i].pgn == pgn_number) { entry = &pgn_index[i]; break; }
    }
    if (!entry) {
        xSemaphoreGive(pgn_db_mutex);
        return NULL;
    }

    // Cache hit: the definition was already parsed on a previous lookup.
    if (entry->def) {
        cJSON *res = entry->def;
        xSemaphoreGive(pgn_db_mutex);
        return res;
    }

    // Miss: read just this object from the file and parse it.
    char *json_str = read_object_at_offset(entry->offset);
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

    // Cache it permanently if we are under the cap; otherwise keep it only in a
    // single scratch slot (freed on the next over-cap miss) to bound memory.
    cJSON *res;
    if (cached_def_count < PGN_DEF_CACHE_CAP) {
        entry->def = pgn_obj;
        cached_def_count++;
        if (cached_def_count == PGN_DEF_CACHE_CAP) {
            ESP_LOGW(TAG, "PGN definition cache full (%d); further PGNs re-read each time",
                     PGN_DEF_CACHE_CAP);
        }
        res = pgn_obj;
    } else {
        if (uncached_scratch) cJSON_Delete(uncached_scratch);
        uncached_scratch = pgn_obj;
        res = pgn_obj;
    }

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

    // Search for "Id": "pgn_id", then map the match back to the enclosing
    // object via the offset index (largest object offset <= match offset).
    char search_str[128];
    snprintf(search_str, sizeof(search_str), "\"Id\": \"%s\"", pgn_id);

    long pat_off = find_pattern_offset(search_str);
    long obj_off = -1;
    if (pat_off >= 0) {
        for (int i = 0; i < pgn_index_count; i++) {
            if (pgn_index[i].offset <= pat_off && pgn_index[i].offset > obj_off) {
                obj_off = pgn_index[i].offset;
            }
        }
    }

    char *json_str = (obj_off >= 0) ? read_object_at_offset(obj_off) : NULL;
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
