/*
 * Example: Load and parse NMEA PGN database using cJSON
 * 
 * This example shows how to:
 * 1. Load the JSON PGN database from SD card
 * 2. Look up specific PGNs
 * 3. Extract field definitions
 */

#include <stdio.h>
#include <esp_log.h>
#include "cJSON.h"
#include "pgn_json_parser.h"

static const char *TAG = "PGN_EXAMPLE";

// Example 1: Load database and print all PGN IDs
void example_load_and_list()
{
    const char *db_path = "/sdcard/PGNS/NMEA_database_1_300.json";
    
    cJSON *pgn_db = pgn_json_load(db_path);
    if (!pgn_db) {
        ESP_LOGE(TAG, "Failed to load PGN database");
        return;
    }

    // Print all available PGNs
    pgn_print_all_ids(pgn_db);

    cJSON_Delete(pgn_db);
}

// Example 2: Find a specific PGN by number
void example_find_pgn_by_number()
{
    const char *db_path = "/sdcard/PGNS/NMEA_database_1_300.json";
    
    cJSON *pgn_db = pgn_json_load(db_path);
    if (!pgn_db) return;

    // Look up System Time PGN (126992)
    cJSON *pgn_def = pgn_get_definition(pgn_db, 126992);
    if (pgn_def) {
        pgn_parse_systemtime(pgn_def);
    } else {
        ESP_LOGW(TAG, "PGN 126992 not found");
    }

    cJSON_Delete(pgn_db);
}

// Example 3: Find a specific PGN by ID string
void example_find_pgn_by_id()
{
    const char *db_path = "/sdcard/PGNS/NMEA_database_1_300.json";
    
    cJSON *pgn_db = pgn_json_load(db_path);
    if (!pgn_db) return;

    // Look up by ID string
    cJSON *pgn_def = pgn_get_definition_by_id(pgn_db, "systemTime");
    if (pgn_def) {
        pgn_parse_systemtime(pgn_def);
    } else {
        ESP_LOGW(TAG, "PGN with ID 'systemTime' not found");
    }

    cJSON_Delete(pgn_db);
}

// Example 4: Custom parsing of a specific field
void example_parse_field()
{
    const char *db_path = "/sdcard/PGNS/NMEA_database_1_300.json";
    
    cJSON *pgn_db = pgn_json_load(db_path);
    if (!pgn_db) return;

    cJSON *pgn_def = pgn_get_definition(pgn_db, 126992);
    if (pgn_def) {
        cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
        
        cJSON *field = NULL;
        cJSON_ArrayForEach(field, fields) {
            cJSON *id = cJSON_GetObjectItem(field, "Id");
            
            // Get all field properties
            cJSON *name = cJSON_GetObjectItem(field, "Name");
            cJSON *bitLength = cJSON_GetObjectItem(field, "BitLength");
            cJSON *bitOffset = cJSON_GetObjectItem(field, "BitOffset");
            cJSON *resolution = cJSON_GetObjectItem(field, "Resolution");
            cJSON *fieldType = cJSON_GetObjectItem(field, "FieldType");
            
            if (id && strcmp(id->valuestring, "sid") == 0) {
                ESP_LOGI(TAG, "Found SID field:");
                ESP_LOGI(TAG, "  Name: %s", name->valuestring);
                ESP_LOGI(TAG, "  BitLength: %d", bitLength->valueint);
                ESP_LOGI(TAG, "  BitOffset: %d", bitOffset->valueint);
                ESP_LOGI(TAG, "  Resolution: %f", resolution->valuedouble);
                ESP_LOGI(TAG, "  Type: %s", fieldType->valuestring);
                break;
            }
        }
    }

    cJSON_Delete(pgn_db);
}

// Example 5: Extract field as C struct (manual approach)
typedef struct {
    uint8_t sid;
    uint8_t source;
    uint16_t date;
    uint32_t time;
} systemTime_t;

void example_build_struct_from_pgn()
{
    const char *db_path = "/sdcard/PGNS/NMEA_database_1_300.json";
    
    cJSON *pgn_db = pgn_json_load(db_path);
    if (!pgn_db) return;

    cJSON *pgn_def = pgn_get_definition(pgn_db, 126992);
    if (pgn_def) {
        cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
        
        // Use field information to configure struct packing
        // This is where you'd generate struct member initialization
        
        systemTime_t time_data = {
            .sid = 0,
            .source = 0,
            .date = 0,
            .time = 0
        };
        
        ESP_LOGI(TAG, "SystemTime struct created from PGN definition");
        ESP_LOGI(TAG, "Size: %zu bytes", sizeof(systemTime_t));
    }

    cJSON_Delete(pgn_db);
}
