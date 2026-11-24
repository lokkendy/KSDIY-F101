#include "voice.h"

#include "app_settings.h"
#include "i18n.h"
#include "ksdiy_audio_feedback.h"
#include "esp_log.h"

static const voice_clip_t *clips[2];
static size_t clip_count;

void voice_init(const voice_clip_t *zh, const voice_clip_t *en, size_t count)
{
    if (clip_count || !zh || !en || !count) return;
    if (!ksdiy_feedback_start()) {
        ESP_LOGW("voice", "audio feedback task allocation failed");
        return;
    }
    clips[0] = zh;
    clips[1] = en;
    clip_count = count;
    ESP_LOGI("voice", "touch feedback and bilingual prompts ready");
}

void voice_touch_click(void)
{
    const app_settings_t *cfg = settings_get();
    if (cfg->touch_sound) ksdiy_feedback_touch(cfg->volume);
}

void voice_play(int id)
{
    const app_settings_t *cfg = settings_get();
    if (!cfg->voice || id < 0 || (size_t)id >= clip_count) return;
    const voice_clip_t *clip = &clips[i18n_get_lang() == APP_LANG_EN][id];
    ksdiy_feedback_voice(clip->pcm, clip->len, cfg->volume);
}
