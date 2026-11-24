#include "bambu_photo.h"

#include "bambu_photo_png.h"

bool bambu_photo_for_model(bambu_model_t model, const uint8_t **png, size_t *len)
{
    const uint8_t *data = NULL;
    size_t n = 0;

    switch (model) {
    case BAMBU_MODEL_P1P:      data = bambu_photo_p1p;     n = BAMBU_PHOTO_P1P_LEN;     break;
    case BAMBU_MODEL_P1S:      data = bambu_photo_p1s;     n = BAMBU_PHOTO_P1S_LEN;     break;
    case BAMBU_MODEL_X1:
    case BAMBU_MODEL_X1C:      data = bambu_photo_x1c;     n = BAMBU_PHOTO_X1C_LEN;     break;
    case BAMBU_MODEL_X1E:      data = bambu_photo_x1e;     n = BAMBU_PHOTO_X1E_LEN;     break;
    case BAMBU_MODEL_A1:       data = bambu_photo_a1;      n = BAMBU_PHOTO_A1_LEN;      break;
    case BAMBU_MODEL_A1_MINI:  data = bambu_photo_a1mini;  n = BAMBU_PHOTO_A1MINI_LEN;  break;
    case BAMBU_MODEL_H2S:      data = bambu_photo_h2s;     n = BAMBU_PHOTO_H2S_LEN;     break;
    case BAMBU_MODEL_H2C:      data = bambu_photo_h2c;     n = BAMBU_PHOTO_H2C_LEN;     break;
    case BAMBU_MODEL_H2D:      data = bambu_photo_h2d;     n = BAMBU_PHOTO_H2D_LEN;     break;
    default:                   return false;   /* UNKNOWN 由上层 coerce；OTHER 无照片 */
    }

    if (png) {
        *png = data;
    }
    if (len) {
        *len = n;
    }
    return true;
}
