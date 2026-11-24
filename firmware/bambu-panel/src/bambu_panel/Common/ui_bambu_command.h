#pragma once
#include "esp_err.h"
/* Call immediately after a UI command, on the LVGL thread. */
void ui_bambu_command_result(esp_err_t result, const char *action);
/* Called by the global UI timer, survives page navigation. Never waits for ACK. */
void ui_bambu_commands_poll(void);
