// Teste em PC: paridade Python x C++.
// Compilar: g++ -std=c++17 -I wokwi ml/tests/host_test.cpp -o /tmp/host_test && /tmp/host_test ml/tests/parity_vectors.txt
#include <cstdio>
#include "features.h"
#include "classifier.h"

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "uso: host_test parity_vectors.txt\n"); return 2; }
  FILE *fp = fopen(argv[1], "r");
  if (!fp) { perror("abrir vetores"); return 2; }
  int total = 0, ok = 0;
  float w[WINDOW_SIZE], f[N_FEATURES];
  for (;;) {
    int i = 0;
    for (; i < WINDOW_SIZE; i++) if (fscanf(fp, "%f", &w[i]) != 1) break;
    int py;
    if (i < WINDOW_SIZE || fscanf(fp, "%d", &py) != 1) break;
    extractFeatures(w, WINDOW_SIZE, f);
    total++;
    if (classifyWindow(f) == py) ok++;
  }
  fclose(fp);
  printf("Paridade Python x C++: %d/%d iguais\n", ok, total);
  // TODO: casos fixos por classe (mesmos perfis do modo TEST do sketch)
  return (ok == total && total > 0) ? 0 : 1;
}
