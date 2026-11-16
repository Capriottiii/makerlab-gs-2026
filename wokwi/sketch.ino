/*
 * Alaga Alerta — Sensor de nível de água em bueiro/córrego (Maker Lab, FIAP GS 2026.2)
 *
 * ESP32 + HC-SR04 (sensor de proximidade: mede o nível da água) + MQ-2 (gás no bueiro)
 * + DHT22 (contexto) + 3 LEDs + buzzer.
 * IA embarcada (TinyML): árvore de decisão classifica a janela em NORMAL / ATENCAO / PERIGO.
 * Alerta local por LED e buzzer (funciona sem Wi-Fi) e envio das leituras para a API central:
 * POST /leituras. Sem Wi-Fi ou com a API fora do ar, os alertas ficam numa fila e são reenviados.
 *
 * Sem bibliotecas externas: o DHT é lido por um driver próprio (DHT_USAR_BIBLIOTECA 0).
 * Se o DHT só mostrar "[dht] leitura inválida", ponha DHT_USAR_BIBLIOTECA 1 e crie a aba
 * libraries.txt com as linhas "DHT sensor library" e "Adafruit Unified Sensor".
 */
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <math.h>

// ============================ CONFIGURAÇÃO ============================
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6

// Para testar antes da API existir: crie um endereço em webhook.site e cole aqui.
// Na API do MVP, a rota é POST /leituras.
#define API_URL        "https://webhook.site/COLE-SEU-ID-AQUI"
#define DEVICE_ID      "alaga-sensor-01"
#define DEVICE_KEY     "dev-key-demo"   // placeholder: NÃO é segredo real. Chave real fica fora do git.

// Ponto de instalação (EXEMPLO: troque pelo bueiro/córrego real do bairro escolhido)
#define BAIRRO         "Vila Maria"
#define SENSOR_LAT     -23.52140f
#define SENSOR_LON     -46.58780f

// Geometria: o HC-SR04 fica no topo do bueiro, apontado para baixo.
// nível = ALTURA_BUEIRO_CM - distância medida. Distância >= altura = bueiro vazio.
#define ALTURA_BUEIRO_CM     150.0f

// Fonte do nível: 0 = sensor do Wokwi (slider de distância do HC-SR04)
// 1 = NORMAL · 2 = ATENCAO · 3 = PERIGO · 4 = SUBIDA RÁPIDA (nível sobe sozinho; reinicia)
// 5 = GÁS ALTO com nível normal
#define FONTE_NIVEL    0

#define SAMPLE_INTERVAL_MS   500    // intervalo entre leituras (uma janela = 10 leituras = 5 s)
#define CONFIRM_WINDOWS      3      // janelas consecutivas de risco para confirmar o alerta
#define ALERT_COOLDOWN_MS    20000  // não reenviar a MESMA classe de alerta antes disso
#define HEARTBEAT_MS         30000  // com tudo NORMAL, envia uma leitura de rotina a cada 30 s

// ---- MQ-2 (gás combustível/esgoto acumulado no bueiro) ----
// Leitura analógica sem calibração: vale como PORCENTAGEM DA ESCALA (0..100), não como ppm.
// O limite é uma hipótese de projeto; calibrar com o sensor real antes de citar valores.
#define GAS_ALERTA_PCT       60.0f  // média da janela acima disso = gás alto (sobe NORMAL para ATENCAO)

// ---- DHT (temperatura e umidade do local) ----
#define DHT_TIPO             22    // 22 = DHT22 (peça do Wokwi) · 11 = DHT11 (sensor real)
#define DHT_USAR_BIBLIOTECA  0     // 0 = leitura própria, sem biblioteca · 1 = biblioteca Adafruit
#define DHT_INTERVAL_MS      3000
#if DHT_USAR_BIBLIOTECA
  #include <DHT.h>
#endif

// ============================ IA EMBARCADA ============================
#define WINDOW_SIZE 10
#define N_FEATURES  3

#define CLASS_NORMAL  0
#define CLASS_ATENCAO 1
#define CLASS_PERIGO  2
static const char *CLASS_NAMES[3] = {"NORMAL", "ATENCAO", "PERIGO"};

// Atributos da janela: f[0] nível médio (%), f[1] taxa de subida (%/min, regressão linear),
// f[2] nível máximo (%). Mesma conta de ml/train_classifier.py.
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
  float slopePorAmostra = num / den;                       // % por amostra
  out[0] = meanY;
  out[1] = slopePorAmostra * (60000.0f / SAMPLE_INTERVAL_MS);  // % por minuto
  out[2] = maxY;
}

// ================= CLASSIFICADOR TinyML (gerado por ml/train_classifier.py) =================
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
// ============================ PROGRAMA ============================

// ------------------------------- Pinagem -------------------------------
#define PIN_GAS_AO   34   // MQ-2 AOUT (ADC1, ok com Wi-Fi ligado)
#define PIN_GAS_DO   33   // MQ-2 DOUT (ligado, mas não usado: a decisão usa o analógico)
#define PIN_TRIG     19
#define PIN_ECHO     18   // HC-SR04 ECHO é 5 V: no sensor real use divisor de tensão
#define PIN_DHT      32
#define PIN_LED_G    25
#define PIN_LED_Y    26
#define PIN_LED_R    27
#define PIN_BUZZER   23

// --------------------------- Estado do sensor ---------------------------
static const char *SRC_NAMES[] = {"SENSOR", "NORMAL", "ATENCAO", "PERIGO", "SUBIDA_RAPIDA", "GAS_ALTO"};

#if DHT_USAR_BIBLIOTECA
static DHT dht(PIN_DHT, DHT_TIPO);
#endif
static float tempC = NAN, umidPct = NAN;       // última leitura válida do DHT

static float winNivel[WINDOW_SIZE], winGas[WINDOW_SIZE];
static int winCount = 0;
static int badStreak = 0;                      // janelas consecutivas com risco (ATENCAO ou PERIGO)
static int lastClass = CLASS_NORMAL;           // classe da última janela (comanda os LEDs)
static int streakClass = -1;                   // classe que está sendo confirmada
static int lastSrc = -1;
static int lastReportClass = -1;               // classe do último alerta enviado
static int distFalhas = 0;
static uint32_t eventSeq = 0;
static unsigned long lastSampleMs = 0, lastReportMs = 0, lastBeatMs = 0;
static unsigned long lastDhtMs = 0, lastWifiTryMs = 0, srcStartMs = 0;
static float nivelAtualCm = 0, gasAtualPct = 0;
static bool gasAltoAtual = false;

// Fila de alertas (tolerância a falha de rede)
#define QUEUE_SIZE 8
static String queueBuf[QUEUE_SIZE];
static int qHead = 0, qCount = 0;

// ----------------------------- Utilidades -----------------------------
static void setLeds(bool g, bool y, bool r) {
  digitalWrite(PIN_LED_G, g); digitalWrite(PIN_LED_Y, y); digitalWrite(PIN_LED_R, r);
}

static void beep(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(onMs);
    digitalWrite(PIN_BUZZER, LOW);  delay(offMs);
  }
}

static String isoTime() {
  struct tm t;
  if (getLocalTime(&t, 50)) {
    char b[25]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &t); return String(b);
  }
  return String("1970-01-01T00:00:00Z");  // NTP ainda não sincronizou
}

// ------------------------------ Sensores ------------------------------
// HC-SR04: distância até a superfície da água. Retorna NAN se não houve eco (leitura inválida).
static float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000UL);  // timeout 30 ms (~5 m)
  if (us == 0) return NAN;
  return us * 0.0343f / 2.0f;
}

// Nível da água em cm (0 = vazio, ALTURA_BUEIRO_CM = transbordando)
static float nivelDeDistancia(float distCm) {
  float n = ALTURA_BUEIRO_CM - distCm;
  if (n < 0) n = 0;
  if (n > ALTURA_BUEIRO_CM) n = ALTURA_BUEIRO_CM;
  return n;
}

// MQ-2: mediana de 3 leituras (filtra ruído do ADC), em % da escala (0..100)
static float readGasPct() {
  int a = analogRead(PIN_GAS_AO), b = analogRead(PIN_GAS_AO), c = analogRead(PIN_GAS_AO);
  if (a > b) { int x = a; a = b; b = x; }
  if (b > c) { int x = b; b = c; c = x; }
  if (a > b) { int x = a; a = b; b = x; }
  return b / 4095.0f * 100.0f;
}

static float noise(float amp) { return amp * ((random(-1000, 1001)) / 1000.0f); }

// Perfis de teste (FONTE_NIVEL != 0). Níveis em cm.
static void leituraPerfil(int src, float &nivelCm, float &gasPct) {
  gasPct = 10.0f + noise(2.0f);   // gás baixo, salvo no perfil 5
  float seg = (millis() - srcStartMs) / 1000.0f;
  switch (src) {
    case 1: nivelCm = 15.0f + noise(1.0f);  break;   // NORMAL
    case 2: nivelCm = 75.0f + noise(1.0f);  break;   // ATENCAO
    case 3: nivelCm = 125.0f + noise(1.0f); break;   // PERIGO
    case 5: nivelCm = 15.0f + noise(1.0f); gasPct = 85.0f + noise(3.0f); break;   // GÁS ALTO
    default: {                                        // 4: sobe 0,5 cm/s e reinicia
      float n = 20.0f + 0.5f * fmodf(seg, 240.0f);
      nivelCm = n + noise(0.5f);
    }
  }
}

static int fonteAtual() { return FONTE_NIVEL; }

// ---- Leitura do DHT sem biblioteca (protocolo de 1 fio, 40 bits) ----
#if !DHT_USAR_BIBLIOTECA
#define DHT_TIMEOUT    0xFFFFFFFFUL
#define DHT_LIMITE_US  1000
static uint32_t dhtPulso(int nivel) {
  uint32_t t0 = micros();
  while (digitalRead(PIN_DHT) == nivel) { if (micros() - t0 > DHT_LIMITE_US) return DHT_TIMEOUT; }
  return micros() - t0;
}
static bool dhtEsperar(int nivel) {            // espera a linha chegar ao nível
  uint32_t t0 = micros();
  while (digitalRead(PIN_DHT) != nivel) { if (micros() - t0 > DHT_LIMITE_US) return false; }
  return true;
}
static const char *dhtEtapa = "";              // onde a última leitura falhou (diagnóstico)
#endif

static bool dhtLerHw(float &h, float &t) {
#if DHT_USAR_BIBLIOTECA
  h = dht.readHumidity(); t = dht.readTemperature();
  return !(isnan(h) || isnan(t));
#else
  uint8_t d[5] = {0, 0, 0, 0, 0};
  uint32_t baixo[40], alto[40];
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delay(DHT_TIPO == 11 ? 20 : 2);
  noInterrupts();
  pinMode(PIN_DHT, INPUT_PULLUP);
  dhtEtapa = "resposta";
  bool ok = dhtEsperar(LOW)
         && dhtPulso(LOW) != DHT_TIMEOUT
         && dhtPulso(HIGH) != DHT_TIMEOUT;
  for (int i = 0; ok && i < 40; i++) {
    baixo[i] = dhtPulso(LOW);
    alto[i]  = dhtPulso(HIGH);
    if (baixo[i] == DHT_TIMEOUT || alto[i] == DHT_TIMEOUT) { ok = false; dhtEtapa = "bits"; }
  }
  interrupts();
  if (!ok) return false;
  for (int i = 0; i < 40; i++) { d[i / 8] <<= 1; if (alto[i] > baixo[i]) d[i / 8] |= 1; }
  if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) { dhtEtapa = "checksum"; return false; }
  if (DHT_TIPO == 11) {
    h = d[0] + d[1] / 10.0f;
    t = (d[2] & 0x7F) + (d[3] & 0x0F) / 10.0f; if (d[2] & 0x80) t = -t;
  } else {
    h = ((d[0] << 8) | d[1]) / 10.0f;
    t = (((d[2] & 0x7F) << 8) | d[3]) / 10.0f; if (d[2] & 0x80) t = -t;
  }
  return true;
#endif
}

// Mantém a última leitura válida; se a leitura falhar, registra e conserva a anterior
static void lerDht() {
  float h, t;
  for (int tentativa = 0; tentativa < 2; tentativa++) {
    if (dhtLerHw(h, t) && h >= 0 && h <= 100) { umidPct = h; tempC = t; return; }
    delay(50);
  }
#if DHT_USAR_BIBLIOTECA
  Serial.println("[dht] leitura inválida — mantendo a anterior");
#else
  Serial.printf("[dht] leitura inválida (etapa: %s) — mantendo a anterior\n", dhtEtapa);
#endif
}

// ------------------------------- Rede --------------------------------
static void connectWifi() {
  Serial.print("[wifi] conectando");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ok");
    configTime(0, 0, "pool.ntp.org");
  } else {
    Serial.println(" FALHOU (modo offline: alertas locais continuam e ficam na fila)");
  }
}

// Retorna true se a API aceitou (HTTP 2xx)
static bool postJson(const String &body) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  WiFiClientSecure secure;
  bool https = String(API_URL).startsWith("https");
  if (https) {
    secure.setInsecure();  // SÓ para demo/simulador. Em produção: validar o certificado (setCACert).
    http.begin(secure, API_URL);
  } else {
    http.begin(API_URL);
  }
  http.setTimeout(6000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Id", DEVICE_ID);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  int code = http.POST(body);
  http.end();
  Serial.printf("[api] POST -> HTTP %d\n", code);
  return code >= 200 && code < 300;
}

static void enqueue(const String &body) {
  if (qCount == QUEUE_SIZE) {  // fila cheia: descarta a mais antiga (e registra)
    Serial.println("[fila] cheia — descartando o alerta mais antigo");
    qHead = (qHead + 1) % QUEUE_SIZE; qCount--;
  }
  queueBuf[(qHead + qCount) % QUEUE_SIZE] = body;
  qCount++;
  Serial.printf("[fila] %d pendente(s)\n", qCount);
}

static void flushQueue() {
  while (qCount > 0) {
    if (!postJson(queueBuf[qHead])) return;  // ainda sem rede: tenta depois
    queueBuf[qHead] = "";
    qHead = (qHead + 1) % QUEUE_SIZE; qCount--;
    Serial.printf("[fila] reenviada, restam %d\n", qCount);
  }
}

// ------------------------- Montagem da leitura -------------------------
static void fmtNum(char *out, size_t n, float v, const char *fmt) {
  if (isnan(v)) snprintf(out, n, "null"); else snprintf(out, n, fmt, v);   // JSON válido sem leitura
}

// tipo: "alerta" (ATENCAO/PERIGO confirmado) ou "leitura" (rotina com tudo NORMAL)
static String buildPayload(int cls, const float *f, const char *tipo, const char *motivo, int src) {
  char sT[12], sH[12];
  fmtNum(sT, sizeof sT, tempC, "%.1f");
  fmtNum(sH, sizeof sH, umidPct, "%.0f");
  char buf[720];
  snprintf(buf, sizeof buf,
    "{\"sensor_id\":\"%s\",\"event_id\":\"%s-%lu\",\"ts\":\"%s\",\"tipo\":\"%s\","
    "\"bairro\":\"%s\",\"lat\":%.5f,\"lon\":%.5f,"
    "\"classe\":\"%s\",\"nivel_cm\":%.1f,\"nivel_pct\":%.1f,\"taxa_pct_min\":%.1f,"
    "\"gas_pct\":%.0f,\"gas_alto\":%s,\"motivo\":\"%s\",\"janelas_confirmadas\":%d,"
    "\"temperatura_c\":%s,\"umidade_pct\":%s,\"fonte\":\"%s\"}",
    DEVICE_ID, DEVICE_ID, (unsigned long)eventSeq, isoTime().c_str(), tipo,
    BAIRRO, SENSOR_LAT, SENSOR_LON,
    CLASS_NAMES[cls], f[0] * ALTURA_BUEIRO_CM / 100.0f, f[0], f[1],
    gasAtualPct, gasAltoAtual ? "true" : "false", motivo,
    tipo[0] == 'a' ? CONFIRM_WINDOWS : 1, sT, sH,
    src == 0 ? "sensor" : "teste");
  return String(buf);
}

// ------------------------------- Setup -------------------------------
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  pinMode(PIN_TRIG, OUTPUT); pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_Y, OUTPUT); pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_GAS_DO, INPUT);
  analogReadResolution(12);
#if DHT_USAR_BIBLIOTECA
  dht.begin();
#endif
  setLeds(false, false, false);
  Serial.println("\n=== Alaga Alerta · Sensor de nível ===");
  connectWifi();
  lerDht();
  Serial.printf("[monitor] INICIADO (automático) · bairro %s · bueiro de %.0f cm\n", BAIRRO, ALTURA_BUEIRO_CM);
  setLeds(true, false, false);
}

// ------------------------------- Loop --------------------------------
// Todo o monitoramento acontece aqui: sensores → IA → decisão → alerta local → envio.
void loop() {
  unsigned long now = millis();

  // ---- Fonte: se mudou, descarta a janela misturada ----
  int src = fonteAtual();
  if (src != lastSrc) {
    lastSrc = src; winCount = 0; badStreak = 0; streakClass = -1; srcStartMs = now;
    Serial.printf("[fonte] nível: %s\n", SRC_NAMES[src]);
  }

  // ---- Rede: tenta reconectar a cada 10 s se caiu ----
  if (WiFi.status() != WL_CONNECTED && now - lastWifiTryMs > 10000) {
    lastWifiTryMs = now; WiFi.reconnect();
  }

  // ---- DHT: temperatura e umidade do local ----
  if (now - lastDhtMs >= DHT_INTERVAL_MS) { lastDhtMs = now; lerDht(); }

  // ---- LEDs seguem a classe da última janela ----
  if (lastClass == CLASS_PERIGO)       setLeds(false, false, true);
  else if (lastClass == CLASS_ATENCAO) setLeds(false, true, false);
  else                                 setLeds(true, false, false);

  // ---- Amostragem do nível ----
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) { delay(5); return; }
  lastSampleMs = now;

  float nivelCm, gasPct;
  if (src == 0) {
    float d = readDistanceCm();
    if (isnan(d)) {                       // sem eco: ignora a amostra, não classifica com dado ruim
      if (++distFalhas % 10 == 1) Serial.println("[sensor] HC-SR04 sem eco — amostra ignorada");
      return;
    }
    distFalhas = 0;
    nivelCm = nivelDeDistancia(d);
    gasPct = readGasPct();
  } else {
    leituraPerfil(src, nivelCm, gasPct);
  }
  if (nivelCm < 0) nivelCm = 0;
  if (nivelCm > ALTURA_BUEIRO_CM) nivelCm = ALTURA_BUEIRO_CM;
  nivelAtualCm = nivelCm;
  if (gasPct < 0) gasPct = 0;
  if (gasPct > 100) gasPct = 100;
  winNivel[winCount] = nivelCm * 100.0f / ALTURA_BUEIRO_CM;
  winGas[winCount] = gasPct;
  winCount++;
  if (winCount < WINDOW_SIZE) return;
  winCount = 0;

  // ---- Janela completa: extrai atributos e classifica (TinyML) ----
  float f[N_FEATURES];
  extractFeatures(winNivel, WINDOW_SIZE, f);
  int cls = classifyWindow(f);                       // risco de alagamento (TinyML)
  float gasMedio = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) gasMedio += winGas[i];
  gasMedio /= WINDOW_SIZE;
  bool gasAlto = gasMedio >= GAS_ALERTA_PCT;
  const char *motivo = "nivel";
  if (gasAlto && cls == CLASS_NORMAL) { cls = CLASS_ATENCAO; motivo = "gas"; }   // gás sobe NORMAL para ATENCAO
  else if (gasAlto)                    { motivo = "nivel+gas"; }
  lastClass = cls;
  gasAtualPct = gasMedio; gasAltoAtual = gasAlto;
  Serial.printf("[ia] nivel=%.1f cm (%.0f%%) taxa=%.1f %%/min | gas=%.0f%% %s -> %s | T=%.1fC U=%.0f%%\n",
                f[0] * ALTURA_BUEIRO_CM / 100.0f, f[0], f[1], gasMedio, gasAlto ? "(ALTO)" : "", CLASS_NAMES[cls], tempC, umidPct);

  // ---- NORMAL: zera a contagem, reenvia a fila e manda leitura de rotina ----
  if (cls == CLASS_NORMAL) {
    badStreak = 0; streakClass = -1;
    flushQueue();
    if (lastBeatMs == 0 || now - lastBeatMs >= HEARTBEAT_MS) {
      lastBeatMs = now;
      eventSeq++;
      String body = buildPayload(cls, f, "leitura", motivo, src);
      Serial.println("[leitura] " + body);
      if (!postJson(body)) Serial.println("[api] leitura de rotina não enviada (sem fila: a próxima cobre)");
    }
    return;
  }

  // ---- Risco (ATENCAO ou PERIGO): histerese de janelas consecutivas ----
  // Mudar de classe reinicia a contagem para a nova classe.
  if (cls != streakClass) { streakClass = cls; badStreak = 0; }
  badStreak++;
  if (badStreak < CONFIRM_WINDOWS) return;   // evita falso alarme por onda ou ruído passageiro
  badStreak = 0;

  // ---- Alerta confirmado: cooldown vale só para a MESMA classe ----
  if (lastReportMs != 0 && cls == lastReportClass && now - lastReportMs < ALERT_COOLDOWN_MS) {
    Serial.println("[alerta] já reportado há pouco — ignorando (cooldown)");
    return;
  }
  lastReportMs = now;
  lastReportClass = cls;
  // Alerta LOCAL primeiro: não depende de rede
  if (cls == CLASS_PERIGO) beep(3, 400, 120); else beep(2, 150, 100);
  eventSeq++;
  String body = buildPayload(cls, f, "alerta", motivo, src);
  Serial.println("[alerta] CONFIRMADO: " + body);
  if (!postJson(body)) { Serial.println("[api] falha — guardando na fila"); enqueue(body); }
}