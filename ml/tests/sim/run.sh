#!/bin/bash
# Roda o wokwi/sketch.ino REAL em PC (stubs de Arduino + relógio simulado) e imprime o Serial.
# Uso: bash ml/tests/sim/run.sh <teste 1..16>
#   1 vazio · 2 nível médio · 3 nível alto · 4 subida rápida · 5 onda · 6 gás · 7 gás+nível · 8 persistente
#   9 ATENCAO→PERIGO · 11 API fora · 12 sem Wi-Fi · 13 API volta · 14 sem eco · 15 DHT falha · 16 DHT nunca leu
set -e
cd "$(dirname "$0")"
T=${1:?teste}; F=0; [ "$T" = 4 ] && F=4; [ "$T" = 6 ] && F=5
OUT=$(mktemp -d)
sed -e "s/^#define FONTE_NIVEL .*/#define FONTE_NIVEL $F/" -e "s/^#define DHT_USAR_BIBLIOTECA .*/#define DHT_USAR_BIBLIOTECA 1/" ../../../wokwi/sketch.ino > "$OUT/sketch.ino"
g++ -std=c++17 -w -include stub.h -Iinc -I"$OUT" -DSKETCH='"sketch.ino"' driver.cpp -o "$OUT/sim"
"$OUT/sim" "$T"
