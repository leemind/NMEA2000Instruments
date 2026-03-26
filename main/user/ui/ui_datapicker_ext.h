#ifndef UI_DATAPICKER_EXT_H
#define UI_DATAPICKER_EXT_H

#include "lvgl.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the datapicker extension logic */
void ui_datapicker_ext_init(void);

/* Called when the Datapicker screen is loaded */
void ui_datapicker_ext_load(void);

/* Called when a databox is long-pressed to record which one we are editing */
void ui_datapicker_ext_set_edit_index(int index);

/* Save the current pickers to the edited databox */
void ui_datapicker_ext_save(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_DATAPICKER_EXT_H */
