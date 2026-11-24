#include "ui_bambu_command.h"
#include "bambu_command_feedback.h"
#include "bambu_service.h"
#include "ui_common.h"
#include "page_manager.h"
#include "i18n.h"
#include "ui_theme.h"
#include "data_bridge.h"
#include "esp_timer.h"
#include <stdio.h>

static bambu_feedback_t s_feedback;

static void notify(const char *action, bambu_feedback_state_t state, const char *reason)
{
    char text[192];
    const char *format = state == BCF_PENDING ? tr("%s：已发送，等待打印机确认") :
                         state == BCF_ACCEPTED ? tr("%s：打印机已接受指令") :
                         state == BCF_REJECTED ? tr("%s：打印机拒绝了指令") :
                                                tr("%s：结果未确认，请查看实际状态");
    snprintf(text, sizeof(text), format, tr(action));
    ui_toast(text, state == BCF_REJECTED ? UI_TOAST_ERROR : UI_TOAST_INFO);
    if (state != BCF_PENDING)
        bridge_add_log("[Bambu] %s (%s)", text, reason ? reason : "");
}

static bambu_feedback_state_t query(uint32_t sequence, char *reason, size_t cap)
{
    switch (bambu_service_ack_query(sequence, reason, cap)) {
    case BAMBU_ACK_PENDING: return BCF_PENDING;
    case BAMBU_ACK_OK: return BCF_ACCEPTED;
    case BAMBU_ACK_FAIL: return BCF_REJECTED;
    default: return BCF_UNKNOWN;
    }
}

void ui_bambu_command_result(esp_err_t rc, const char *action)
{
    if (rc != ESP_OK) {
        char text[192];
        snprintf(text, sizeof(text), tr("%s：未发送，请检查连接和当前状态"), tr(action));
        ui_toast(text, UI_TOAST_ERROR);
        bridge_add_log("[Bambu] %s (%d)", text, (int)rc);
        return;
    }
    uint32_t seq = bambu_service_last_seq();
    if (!seq) { notify(action, BCF_UNKNOWN, "no_sequence"); return; }
    bambu_feedback_begin(&s_feedback, seq, action, esp_timer_get_time(), notify);
}

void ui_bambu_commands_poll(void)
{
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    bambu_feedback_poll(&s_feedback, esp_timer_get_time(), snap.connected, query, notify);
}
