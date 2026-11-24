#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bambu_status.h"
#include "bambu_service.h"

#define BAMBU_CLOUD_ACCOUNT_MAX 128
#define BAMBU_CLOUD_CODE_MAX 11
#define BAMBU_CLOUD_DEVICE_MAX 16

typedef struct {
    char serial[41];
    char name[80];
    char model[32];
    char state[24];
    bool online_known;
    bool online;
} bambu_cloud_device_t;

typedef struct {
    bool signed_in;
    bool busy;
    bool live;
    unsigned cooldown_seconds;
    unsigned count;
    unsigned selected;
    unsigned catalog_revision;
    uint16_t metrics;
    char message[160];
    bambu_status_t status;
} bambu_cloud_view_t;

enum {
    BAMBU_CLOUD_STATE = 1u << 0,
    BAMBU_CLOUD_PROGRESS = 1u << 1,
    BAMBU_CLOUD_REMAINING = 1u << 2,
    BAMBU_CLOUD_NOZZLE = 1u << 3,
    BAMBU_CLOUD_BED = 1u << 4,
    BAMBU_CLOUD_CHAMBER = 1u << 5,
};

typedef struct {
    bambu_cloud_device_t device;
    bambu_print_state_t state;
    int progress_percent;
    int remaining_minutes;
    bool has_data;
    bool live;
    bool subscription_denied;
} bambu_cloud_fleet_device_t;

typedef struct {
    bool signed_in;
    bool busy;
    unsigned count;
    unsigned selected;
    unsigned catalog_revision;
    bambu_cloud_fleet_device_t devices[BAMBU_CLOUD_DEVICE_MAX];
} bambu_cloud_fleet_t;

void bambu_cloud_fleet_snapshot(bambu_cloud_fleet_t *out);

void bambu_cloud_start(void);
bool bambu_cloud_send_code(const char *account);
bool bambu_cloud_verify(const char *account, const char *code);
bool bambu_cloud_refresh(void);
bool bambu_cloud_select(unsigned index, unsigned catalog_revision);
bool bambu_cloud_sign_out(void);
bool bambu_cloud_media_credentials(const char *serial, bambu_lan_config_t *out);
bool bambu_cloud_set_media(const bambu_lan_config_t *cfg);
bool bambu_cloud_copy_media(bambu_lan_config_t *out);
void bambu_cloud_selected_snapshot(bambu_cloud_view_t *out, bambu_cloud_device_t *selected);
void bambu_cloud_snapshot(bambu_cloud_view_t *out,
                          bambu_cloud_device_t devices[BAMBU_CLOUD_DEVICE_MAX]);
