#ifndef FGB_TYPES_H
#define FGB_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

// Console model selection
// DMG: Original Game Boy (mono)
// CGB: Game Boy Color (color, banking, extra registers)
typedef enum fgb_model {
    FGB_MODEL_DMG = 0,
    FGB_MODEL_CGB = 1
} fgb_model;

#ifdef __cplusplus
}
#endif

#endif // FGB_TYPES_H
