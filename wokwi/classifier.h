// classifier.h — GERADO por ml/train_classifier.py (não editar à mão).
// Atributos: f[0] nível médio (%), f[1] taxa de subida (%/min), f[2] nível máximo (%).
#pragma once
#define CLASS_NORMAL   0
#define CLASS_ATENCAO  1
#define CLASS_PERIGO   2

static const char *CLASS_NAMES[3] = {"NORMAL", "ATENCAO", "PERIGO"};

static inline int classifyWindow(const float *f) {
  if (f[0] <= 40.003357f) {
    if (f[1] <= 4.011576f) {
      return 0;  // NORMAL
    } else {
      if (f[0] <= 10.028100f) {
        return 0;  // NORMAL
      } else {
        if (f[0] <= 29.986534f) {
          if (f[1] <= 9.989774f) {
            return 0;  // NORMAL
          } else {
            return 1;  // ATENCAO
          }
        } else {
          if (f[1] <= 16.059344f) {
            return 1;  // ATENCAO
          } else {
            return 2;  // PERIGO
          }
        }
      }
    }
  } else {
    if (f[0] <= 70.004028f) {
      if (f[1] <= 7.971433f) {
        return 1;  // ATENCAO
      } else {
        if (f[2] <= 45.888361f) {
          if (f[1] <= 16.154346f) {
            return 1;  // ATENCAO
          } else {
            return 2;  // PERIGO
          }
        } else {
          if (f[0] <= 44.965393f) {
            return 2;  // PERIGO
          } else {
            return 2;  // PERIGO
          }
        }
      }
    } else {
      return 2;  // PERIGO
    }
  }
}
