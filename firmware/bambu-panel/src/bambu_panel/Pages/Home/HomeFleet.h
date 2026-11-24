#pragma once

#include "bambu_cloud.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *card, *name, *model, *state, *progress, *remaining, *bar;
    unsigned index;
} HomeFleetCard_t;

typedef struct {
    lv_obj_t *root, *content, *grid, *summary, *filters, *empty, *hint, *login;
    lv_obj_t *filter_buttons[5];
    HomeFleetCard_t cards[BAMBU_CLOUD_DEVICE_MAX];
    lv_event_cb_t on_printer;
    unsigned card_count;
    unsigned active_filter;
} HomeFleet_t;

void HomeFleet_Create(HomeFleet_t *view, lv_obj_t *parent, lv_event_cb_t on_menu,
                      lv_event_cb_t on_printer, lv_event_cb_t on_filter);
void HomeFleet_Update(HomeFleet_t *view, const bambu_cloud_fleet_t *snapshot, const char *notice);
void HomeFleet_SelectFilter(HomeFleet_t *view, unsigned filter);
