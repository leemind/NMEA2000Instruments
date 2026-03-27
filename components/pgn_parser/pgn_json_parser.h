/*
 * pgn_json_parser.h - Helper functions to parse NMEA PGN definitions from JSON
 * Using cJSON (built-in ESP-IDF library)
 */

#pragma once

#include "cJSON.h"
#include <stdbool.h>

/**
 * Parse a single PGN field definition from JSON object
 */
typedef struct {
    int order;
    const char *id;
    const char *name;
    int bitLength;
    int bitOffset;
    int bitStart;
    double resolution;
    bool is_signed;
    int rangeMin;
    int rangeMax;
    const char *fieldType;
    const char *unit;
    const char *typeInPdf;
} pgn_field_t;

/**
 * Parse a PGN definition from JSON object
 */
typedef struct {
    int pgn;
    const char *id;
    const char *description;
    int priority;
    const char *type;
    bool complete;
    int fieldCount;
    int length;
    pgn_field_t *fields;
} pgn_definition_t;

/**
 * Load and parse the JSON PGN database
 * Returns a cJSON object or NULL on error
 */
cJSON *pgn_json_load(const char *filepath);

/**
 * Get PGN definition by PGN number
 */
cJSON *pgn_get_definition(cJSON *pgn_db, int pgn_number);

/**
 * Get PGN definition by ID string
 */
cJSON *pgn_get_definition_by_id(cJSON *pgn_db, const char *pgn_id);

/**
 * Example: Print all PGN IDs and numbers
 */
void pgn_print_all_ids(cJSON *pgn_db);

/**
 * Example: Parse specific PGN and extract fields
 */
void pgn_parse_systemtime(cJSON *pgn_def);

/**
 * Results for streaming PGN search
 */
typedef struct {
    int pgn;
    char description[128];
} pgn_search_result_t;

/**
 * Search PGN database by description query (streaming).
 * Returns the number of results found.
 */
int pgn_search_by_description(const char *query, pgn_search_result_t *results, int max_results);
