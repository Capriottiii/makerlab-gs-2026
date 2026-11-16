// features.h — extração de atributos da janela de leituras de NÍVEL (em % da altura do bueiro).
// C++ puro (sem Arduino): compilado igual no teste de paridade em PC.
// Precisa ser IDÊNTICO a extractFeatures() em wokwi/sketch.ino e a extract_features() em
// ml/train_classifier.py (mesma conta, float32, mesma ordem de operações).
#pragma once
#include <math.h>

#define WINDOW_SIZE 10
#define SAMPLE_INTERVAL_MS 500   // igual ao sketch.ino e ao SAMPLE_MS do treino
#define N_FEATURES  3

// Atributos (ordem fixa!)
//   [0] média do nível na janela (%)
//   [1] taxa de subida (%/min), por regressão linear sobre as amostras
//   [2] nível máximo da janela (%)
static inline void extractFeatures(const float *nivelPct, int n, float *out) {
  float meanY = 0, maxY = 0;
  for (int i = 0; i < n; i++) {
    meanY += nivelPct[i];
    if (nivelPct[i] > maxY) maxY = nivelPct[i];
  }
  meanY /= n;
  float num = 0, den = 0, mid = (n - 1) / 2.0f;
  for (int i = 0; i < n; i++) {
    float di = i - mid;
    num += di * (nivelPct[i] - meanY);
    den += di * di;
  }
  float slopePorAmostra = num / den;                            // % por amostra
  out[0] = meanY;
  out[1] = slopePorAmostra * (60000.0f / SAMPLE_INTERVAL_MS);   // % por minuto
  out[2] = maxY;
}
