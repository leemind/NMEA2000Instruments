#include "ui_datapicker_ext.h"
#include "ui.h"
#include "pgn_json_parser.h"
#include "can.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "UI_PICKER_EXT";

static void populate_field_dropdown(lv_obj_t *dropdown, uint32_t pgn_num);
static void ui_event_field_selected(lv_event_t *e);

static int g_editing_index = -1;
static lv_obj_t *ui_Pgn1Input;
static lv_obj_t *ui_Field1Dropdown;
static lv_obj_t *ui_LabelInput;
static lv_obj_t *ui_UnitInput;
static lv_obj_t *ui_DisplayUnitDropdown;
static lv_obj_t *ui_Pgn2Input;
static lv_obj_t *ui_Field2Dropdown;
static lv_obj_t *ui_Pgn2Label;
static lv_obj_t *ui_Field2Label;
static lv_obj_t *ui_AddSecondCheckbox;
static lv_obj_t *ui_Keyboard;
static lv_obj_t *ui_SearchList;

/* Event when a search result is clicked */
static void ui_event_search_result_clicked(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_list_get_btn_text(ui_SearchList, btn);
    
    /* Parse PGN from "PGN - Description" */
    uint32_t pgn = atoi(txt);
    char pgn_str[16];
    snprintf(pgn_str, sizeof(pgn_str), "%lu", (unsigned long)pgn);
    
    /* Update input and hide list */
    lv_textarea_set_text(ui_Pgn1Input, pgn_str);
    lv_obj_add_flag(ui_SearchList, LV_OBJ_FLAG_HIDDEN);
    
    /* Trigger field population */
    populate_field_dropdown(ui_Field1Dropdown, pgn);
    ui_event_field_selected(NULL);
}

/* Helper to populate search list based on query */
static void populate_search_list(const char *query) {
    if (!ui_SearchList) return;
    lv_obj_clean(ui_SearchList);
    
    if (!query || strlen(query) < 2) {
        lv_obj_add_flag(ui_SearchList, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!pgn_database) return;

    pgn_search_result_t results[10];
    int match_count = pgn_search_by_description(query, results, 10);

    for (int i = 0; i < match_count; i++) {
        char buf[160];
        snprintf(buf, sizeof(buf), "%d - %s", results[i].pgn, results[i].description);
        lv_obj_t *btn = lv_list_add_btn(ui_SearchList, NULL, buf);
        lv_obj_add_event_cb(btn, ui_event_search_result_clicked, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(btn, 0, 0); // No extra border for buttons
    }

    if (match_count > 0) {
        lv_obj_clear_flag(ui_SearchList, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui_SearchList);
    } else {
        lv_obj_add_flag(ui_SearchList, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Event when a textarea is focused, to show keyboard */
static void ui_event_textarea_focused(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (!ui_Keyboard) return;
    
    lv_keyboard_set_textarea(ui_Keyboard, ta);
    
    /* Set mode: text for everything to allow searching by description */
    lv_keyboard_set_mode(ui_Keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    
    lv_obj_clear_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_Keyboard);
}

/* Event when keyboard is closed or "Ready" is pressed */
static void ui_event_keyboard_ready(lv_event_t *e) {
    if (!ui_Keyboard) return;
    lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* Helper to populate a dropdown with field names for a given PGN */
static void populate_field_dropdown(lv_obj_t *dropdown, uint32_t pgn_num) {
    if (pgn_num == 0xFFFFFFFF) {
        lv_dropdown_set_options(dropdown, "TWS\nTWA");
        return;
    }
    if (!pgn_database) return;
    
    cJSON *pgn_def = pgn_get_definition(pgn_database, pgn_num);
    if (!pgn_def) {
        lv_dropdown_set_options(dropdown, "N/A");
        return;
    }

    cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
    if (!fields) {
        lv_dropdown_set_options(dropdown, "No fields");
        return;
    }

    /* Build options string */
    char options[2048] = {0};
    cJSON *field;
    cJSON_ArrayForEach(field, fields) {
        cJSON *id = cJSON_GetObjectItem(field, "Id");
        cJSON *name = cJSON_GetObjectItem(field, "Name");
        cJSON *type = cJSON_GetObjectItem(field, "FieldType");
        
        if (id && id->valuestring) {
            if (type && type->valuestring && strcmp(type->valuestring, "RESERVED") == 0) continue;
            
            if (options[0]) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
            strncat(options, name && name->valuestring ? name->valuestring : id->valuestring, 
                    sizeof(options) - strlen(options) - 1);
        }
    }
    lv_dropdown_set_options(dropdown, options);
}

/* Helper to populate display unit options based on source unit */
static void populate_display_unit_dropdown(const char *source_unit) {
    if (!ui_DisplayUnitDropdown) return;
    
    if (strcmp(source_unit, "m/s") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "m/s\nknots\nkm/h\nmph");
    } else if (strcmp(source_unit, "m") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "m\nfeet\nfathoms");
    } else if (strcmp(source_unit, "K") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "C\nF\nK");
    } else if (strcmp(source_unit, "Pa") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "Pa\nbar\npsi\ninHg");
    } else if (strcmp(source_unit, "L") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "L\ngal (US)\ngal (Imp)");
    } else if (strcmp(source_unit, "L/h") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "L/h\ngph (US)\ngph (Imp)");
    } else if (strcmp(source_unit, "rad") == 0 || strcmp(source_unit, "deg") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "deg\nrad");
    } else if (strcmp(source_unit, "s") == 0) {
        lv_dropdown_set_options(ui_DisplayUnitDropdown, "s\nmins\nhours\nhh:mm");
    } else {
        /* Default: only the source unit */
        lv_dropdown_set_options(ui_DisplayUnitDropdown, source_unit);
    }
}

/* Event when PGN 1 text changes */
static void ui_event_pgn1_changed(lv_event_t *e) {
    const char *txt = lv_textarea_get_text(ui_Pgn1Input);
    
    /* If it's a number, populate fields directly */
    if (isdigit((unsigned char)txt[0])) {
        uint32_t pgn = atoi(txt);
        populate_field_dropdown(ui_Field1Dropdown, pgn);
        ui_event_field_selected(NULL);
    }
    
    /* Search descriptions */
    populate_search_list(txt);
}

/* Event when Add Second checkbox changes */
static void ui_event_add_second_changed(lv_event_t *e) {
    bool checked = lv_obj_has_state(ui_AddSecondCheckbox, LV_STATE_CHECKED);
    if (checked) {
        lv_obj_clear_flag(ui_Pgn2Input, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_Field2Dropdown, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_Pgn2Label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_Field2Label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui_Pgn2Input, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_Field2Dropdown, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_Pgn2Label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_Field2Label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_datapicker_ext_init(void) {
    /* This should be called once, e.g. after ui_init() */
}

void ui_datapicker_ext_load(void) {
    if (!ui_DatapickerScreen) return;
    if (g_editing_index < 0) return;

    if (!ui_Pgn1Input) {
        /* Container for labels/inputs - widened for 1024x600 screen */
        lv_obj_t *cont = lv_obj_create(ui_DatapickerScreen);
        lv_obj_set_size(cont, 1024, 500);
        lv_obj_set_align(cont, LV_ALIGN_TOP_MID);
        //lv_obj_set_y(cont, 10);
        lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(cont, 255, 0);
        lv_obj_set_style_border_width(cont, 0, 0);

        /* --- COLUMN 1 (LHS) --- */
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "PGN 1:");
        lv_obj_set_pos(lbl, 10, 10);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

        ui_Pgn1Input = lv_textarea_create(cont);
        lv_obj_set_size(ui_Pgn1Input, 150, 40);
        lv_obj_set_pos(ui_Pgn1Input, 90, 0);
        lv_textarea_set_one_line(ui_Pgn1Input, true);
        lv_obj_add_event_cb(ui_Pgn1Input, ui_event_pgn1_changed, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_set_style_bg_color(ui_Pgn1Input, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_Pgn1Input, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_Pgn1Input, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_Pgn1Input, 2, 0);
        lv_obj_set_style_bg_color(ui_Pgn1Input, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(ui_Pgn1Input, 255, LV_PART_CURSOR);

        lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "Field 1:");
        lv_obj_set_pos(lbl, 10, 60);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

        ui_Field1Dropdown = lv_dropdown_create(cont);
        lv_obj_set_size(ui_Field1Dropdown, 250, 40);
        lv_obj_set_pos(ui_Field1Dropdown, 90, 50);
        lv_obj_add_event_cb(ui_Field1Dropdown, ui_event_field_selected, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_set_style_bg_color(ui_Field1Dropdown, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_Field1Dropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_Field1Dropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_Field1Dropdown, 2, 0);

        /* Style the actual list that pops up */
        lv_obj_t *f1_list = lv_dropdown_get_list(ui_Field1Dropdown);
        lv_obj_set_style_bg_color(f1_list, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(f1_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(f1_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(f1_list, 2, 0);

        lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "Label:");
        lv_obj_set_pos(lbl, 10, 110);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

        ui_LabelInput = lv_textarea_create(cont);
        lv_obj_set_size(ui_LabelInput, 200, 40);
        lv_obj_set_pos(ui_LabelInput, 90, 100);
        lv_textarea_set_one_line(ui_LabelInput, true);
        lv_obj_set_style_bg_color(ui_LabelInput, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_LabelInput, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_LabelInput, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_LabelInput, 2, 0);
        lv_obj_set_style_bg_color(ui_LabelInput, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(ui_LabelInput, 255, LV_PART_CURSOR);

        lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "Unit:");
        lv_obj_set_pos(lbl, 10, 160);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

        ui_UnitInput = lv_textarea_create(cont);
        lv_obj_set_size(ui_UnitInput, 100, 40);
        lv_obj_set_pos(ui_UnitInput, 90, 150);
        lv_textarea_set_one_line(ui_UnitInput, true);
        lv_obj_add_state(ui_UnitInput, LV_STATE_DISABLED); // Non-editable
        lv_obj_set_style_bg_color(ui_UnitInput, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_UnitInput, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_UnitInput, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_UnitInput, 2, 0);
        lv_obj_set_style_bg_color(ui_UnitInput, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(ui_UnitInput, 255, LV_PART_CURSOR);

        lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "Show As:");
        lv_obj_set_pos(lbl, 10, 210);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

        ui_DisplayUnitDropdown = lv_dropdown_create(cont);
        lv_obj_set_size(ui_DisplayUnitDropdown, 130, 40);
        lv_obj_set_pos(ui_DisplayUnitDropdown, 90, 200);
        lv_obj_set_style_bg_color(ui_DisplayUnitDropdown, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_DisplayUnitDropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_DisplayUnitDropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_DisplayUnitDropdown, 2, 0);

        lv_obj_t *du_list = lv_dropdown_get_list(ui_DisplayUnitDropdown);
        lv_obj_set_style_bg_color(du_list, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(du_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(du_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(du_list, 2, 0);

        ui_AddSecondCheckbox = lv_checkbox_create(cont);
        lv_checkbox_set_text(ui_AddSecondCheckbox, "Add field?");
        lv_obj_set_pos(ui_AddSecondCheckbox, 10, 250);
        lv_obj_add_event_cb(ui_AddSecondCheckbox, ui_event_add_second_changed, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_set_style_text_color(ui_AddSecondCheckbox, lv_color_hex(0xFFFFFF), 0);

        /* --- COLUMN 2 (RHS) --- */
        ui_Pgn2Label = lv_label_create(cont);
        lv_label_set_text(ui_Pgn2Label, "PGN 2:");
        lv_obj_set_pos(ui_Pgn2Label, 500, 10);
        lv_obj_add_flag(ui_Pgn2Label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(ui_Pgn2Label, lv_color_hex(0xFFFFFF), 0);

        ui_Pgn2Input = lv_textarea_create(cont);
        lv_obj_set_size(ui_Pgn2Input, 150, 40);
        lv_obj_set_pos(ui_Pgn2Input, 580, 0);
        lv_textarea_set_one_line(ui_Pgn2Input, true);
        lv_obj_add_flag(ui_Pgn2Input, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui_Pgn2Input, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_Pgn2Input, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_Pgn2Input, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_Pgn2Input, 2, 0);
        lv_obj_set_style_bg_color(ui_Pgn2Input, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(ui_Pgn2Input, 255, LV_PART_CURSOR);

        ui_Field2Label = lv_label_create(cont);
        lv_label_set_text(ui_Field2Label, "Field 2:");
        lv_obj_set_pos(ui_Field2Label, 500, 60);
        lv_obj_add_flag(ui_Field2Label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(ui_Field2Label, lv_color_hex(0xFFFFFF), 0);

        ui_Field2Dropdown = lv_dropdown_create(cont);
        lv_obj_set_size(ui_Field2Dropdown, 250, 40);
        lv_obj_set_pos(ui_Field2Dropdown, 580, 50);
        lv_obj_add_flag(ui_Field2Dropdown, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui_Field2Dropdown, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_Field2Dropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_Field2Dropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_Field2Dropdown, 2, 0);

        lv_obj_t *f2_list = lv_dropdown_get_list(ui_Field2Dropdown);
        lv_obj_set_style_bg_color(f2_list, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(f2_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(f2_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(f2_list, 2, 0);

        /* Create Keyboard */
        ui_Keyboard = lv_keyboard_create(ui_DatapickerScreen);
        lv_obj_set_width(ui_Keyboard, 1024);
        lv_obj_set_height(ui_Keyboard, 300);
        lv_obj_set_align(ui_Keyboard, LV_ALIGN_BOTTOM_MID);
        lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
        
        lv_obj_set_style_bg_color(ui_Keyboard, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_Keyboard, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Keyboard, lv_color_hex(0xAAAAAA), LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_Keyboard, 255, LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_Keyboard, &lv_font_montserrat_28, LV_PART_ITEMS | LV_STATE_DEFAULT);

        lv_obj_add_event_cb(ui_Keyboard, ui_event_keyboard_ready, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(ui_Keyboard, ui_event_keyboard_ready, LV_EVENT_CANCEL, NULL);

        /* Attach focus events to all textareas */
        lv_obj_add_event_cb(ui_Pgn1Input, ui_event_textarea_focused, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(ui_Pgn2Input, ui_event_textarea_focused, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(ui_LabelInput, ui_event_textarea_focused, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(ui_UnitInput, ui_event_textarea_focused, LV_EVENT_FOCUSED, NULL);

        /* Create Search List - child of container now */
        ui_SearchList = lv_list_create(cont);
        lv_obj_set_size(ui_SearchList, 400, 200);
        lv_obj_set_pos(ui_SearchList, 90, 45); // Just below Pgn1Input
        lv_obj_add_flag(ui_SearchList, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui_SearchList, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(ui_SearchList, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(ui_SearchList, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(ui_SearchList, 2, 0);
    }

    /* Reset Keyboard state */
    if (ui_Keyboard) {
        lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ui_Keyboard, NULL);
    }

    /* Populate with current settings */
    app_settings_t settings = settings_get();
    databox_config_t *cfg = &settings.databoxes[g_editing_index];

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)cfg->pgn1);
    lv_textarea_set_text(ui_Pgn1Input, buf);
    populate_field_dropdown(ui_Field1Dropdown, cfg->pgn1);
    ui_event_field_selected(NULL); // Call to update label/unit based on initial selection
    
    /* Find field in dropdown by ID */
    /* Note: if we want to be robust we should map ID to index */
    /* For now let's just use the name from config */
    /* ... TODO: select the item in dropdown ... */

    lv_textarea_set_text(ui_LabelInput, cfg->label);
    lv_textarea_set_text(ui_UnitInput, cfg->unit);
    populate_display_unit_dropdown(cfg->unit);
    
    /* Set selection in display unit dropdown */
    const char *opts = lv_dropdown_get_options(ui_DisplayUnitDropdown);
    if (opts) {
        /* Search for cfg->display_unit in options */
        const char *p = opts;
        uint16_t current_idx = 0;
        while(p) {
            if (strncmp(p, cfg->display_unit, strlen(cfg->display_unit)) == 0 && 
                (p[strlen(cfg->display_unit)] == '\n' || p[strlen(cfg->display_unit)] == '\0')) {
                lv_dropdown_set_selected(ui_DisplayUnitDropdown, current_idx);
                break;
            }
            p = strchr(p, '\n');
            if (p) p++;
            current_idx++;
        }
    }

    if (cfg->pgn2 > 0) {
        lv_obj_add_state(ui_AddSecondCheckbox, LV_STATE_CHECKED);
        lv_obj_clear_flag(ui_Pgn2Input, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_Field2Dropdown, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)cfg->pgn2);
        lv_textarea_set_text(ui_Pgn2Input, buf);
        populate_field_dropdown(ui_Field2Dropdown, cfg->pgn2);
    } else {
        lv_obj_clear_state(ui_AddSecondCheckbox, LV_STATE_CHECKED);
        lv_obj_add_flag(ui_Pgn2Input, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_Field2Dropdown, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_datapicker_ext_set_edit_index(int index) {
    g_editing_index = index;
}

/* Helper to get the ID of the selected field in a dropdown */
static void get_selected_field_id(lv_obj_t *dropdown, uint32_t pgn_num, char *out_id, size_t out_len) {
    uint16_t sel = lv_dropdown_get_selected(dropdown);
    if (pgn_num == 0xFFFFFFFF) {
        if (sel == 0) strncpy(out_id, "TWS", out_len);
        else if (sel == 1) strncpy(out_id, "TWA", out_len);
        return;
    }
    if (!pgn_database) return;
    cJSON *pgn_def = pgn_get_definition(pgn_database, pgn_num);
    if (!pgn_def) return;
    cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
    if (!fields) return;

    int i = 0;
    cJSON *field;
    cJSON_ArrayForEach(field, fields) {
        cJSON *type = cJSON_GetObjectItem(field, "FieldType");
        if (type && type->valuestring && strcmp(type->valuestring, "RESERVED") == 0) continue;
        
        if (i == sel) {
            cJSON *id = cJSON_GetObjectItem(field, "Id");
            if (id && id->valuestring) {
                strncpy(out_id, id->valuestring, out_len - 1);
                out_id[out_len-1] = '\0';
                return;
            }
        }
        i++;
    }
}

/* Event when a field is selected from the dropdown */
static void ui_event_field_selected(lv_event_t *e) {
    uint16_t sel = lv_dropdown_get_selected(ui_Field1Dropdown);
    const char *pgn_txt = lv_textarea_get_text(ui_Pgn1Input);
    uint32_t pgn_num = atoi(pgn_txt);

    if (pgn_num == 0xFFFFFFFF) {
        if (sel == 0) {
            lv_textarea_set_text(ui_LabelInput, "TWS");
            lv_textarea_set_text(ui_UnitInput, "kts");
        } else if (sel == 1) {
            lv_textarea_set_text(ui_LabelInput, "TWA");
            lv_textarea_set_text(ui_UnitInput, "deg");
        }
        return;
    }

    if (!pgn_database) return;
    cJSON *pgn_def = pgn_get_definition(pgn_database, pgn_num);
    if (!pgn_def) return;
    cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
    if (!fields) return;

    int i = 0;
    cJSON *field;
    cJSON_ArrayForEach(field, fields) {
        cJSON *type = cJSON_GetObjectItem(field, "FieldType");
        if (type && type->valuestring && strcmp(type->valuestring, "RESERVED") == 0) continue;
        
        if (i == sel) {
            cJSON *name = cJSON_GetObjectItem(field, "Name");
            cJSON *unit = cJSON_GetObjectItem(field, "Unit");
            
            if (name && name->valuestring) {
                char label[32];
                strncpy(label, name->valuestring, sizeof(label)-1);
                label[sizeof(label)-1] = '\0';
                /* Trim to first space */
                char *space = strchr(label, ' ');
                if (space) *space = '\0';
                /* Convert to uppercase for label style consistency? User choice but usually look better */
                for(int j=0; label[j]; j++) label[j] = toupper((unsigned char)label[j]);
                lv_textarea_set_text(ui_LabelInput, label);
            }
            
            if (unit && unit->valuestring) {
                lv_textarea_set_text(ui_UnitInput, unit->valuestring);
                populate_display_unit_dropdown(unit->valuestring);
            } else {
                lv_textarea_set_text(ui_UnitInput, "");
                populate_display_unit_dropdown("");
            }
            return;
        }
        i++;
    }
}

void ui_datapicker_ext_save(void) {
    if (g_editing_index < 0) return;

    databox_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.pgn1 = atoi(lv_textarea_get_text(ui_Pgn1Input));
    get_selected_field_id(ui_Field1Dropdown, cfg.pgn1, cfg.field1_id, sizeof(cfg.field1_id));

    strncpy(cfg.label, lv_textarea_get_text(ui_LabelInput), sizeof(cfg.label)-1);
    strncpy(cfg.unit, lv_textarea_get_text(ui_UnitInput), sizeof(cfg.unit)-1);
    lv_dropdown_get_selected_str(ui_DisplayUnitDropdown, cfg.display_unit, sizeof(cfg.display_unit)-1);

    if (lv_obj_has_state(ui_AddSecondCheckbox, LV_STATE_CHECKED)) {
        cfg.pgn2 = atoi(lv_textarea_get_text(ui_Pgn2Input));
        get_selected_field_id(ui_Field2Dropdown, cfg.pgn2, cfg.field2_id, sizeof(cfg.field2_id));
    } else {
        cfg.pgn2 = 0;
        cfg.field2_id[0] = '\0';
    }

    settings_set_databox_config(g_editing_index, &cfg);
}
