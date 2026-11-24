#pragma once

#include "Page.h"
#include "bambu_service.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *lbl_status;
    lv_obj_t *dd_model;
    lv_obj_t *lbl_lan_hint;
    lv_obj_t *ta_ip;
    lv_obj_t *ta_serial;
    lv_obj_t *ta_code;
    lv_obj_t *ta_name;
} PrintView_t;

void PrintView_Create(PrintView_t *v, lv_obj_t *parent,
                      const bambu_lan_config_t *saved,
                      lv_event_cb_t on_model_changed,
                      lv_event_cb_t on_discover,
                      lv_event_cb_t on_save,
                      lv_event_cb_t on_pause,
                      lv_event_cb_t on_resume,
                      lv_event_cb_t on_stop,
                      lv_event_cb_t on_speed);
