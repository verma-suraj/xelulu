#include "Cry.h"
#include "../State.h"
#include "../store/Store.h"
#include "../net/Net.h"

#define N       320
#define LAG_MIN 27             // 16000 / 600 Hz
#define LAG_MAX 64             // 16000 / 250 Hz
#define LAG_LO  8              // 2 kHz: search from here so a high tone's period is found first

static uint8_t frames, hits, filled, head;
static float score[10];
static bool episode;
static uint32_t lastCryAt, nextEventAt;
static float cryS;             // cumulative crying seconds in this episode

// Normalised autocorrelation at the fundamental: the FIRST strong peak. ~1 for a steady wail
// pitched 250-600 Hz; 0 for noise and for tones above 600 Hz (their multiples land in band too).
static float harmonicity(const int16_t* x) {
  static float c[N + 1], r[LAG_MAX + 2];          // prefix sums of x^2; correlation per lag
  c[0] = 0;
  for (int i = 0; i < N; i++) c[i + 1] = c[i] + (float)x[i] * x[i];
  for (int lag = LAG_LO; lag <= LAG_MAX + 1; lag++) {
    float num = 0;
    for (int i = 0; i + lag < N; i++) num += (float)x[i] * x[i + lag];
    r[lag] = num / (sqrtf(c[N - lag] * (c[N] - c[lag])) + 1);
  }
  for (int lag = LAG_LO + 1; lag <= LAG_MAX; lag++)
    if (r[lag] >= 0.6f && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) return lag >= LAG_MIN ? r[lag] : 0;
  return 0;
}

static bool cryLike(const int16_t* f, float noiseDb) {
  float e = 0;
  for (int i = 0; i < N; i++) e += (float)f[i] * f[i];
  float db = 10 * log10f(e / N / (32768.0f * 32768.0f) + 1e-12f);
  return db > noiseDb + 12 && db > -45 && harmonicity(f) >= 0.6f;   // crying is loud AND voiced
}

void cryPause() { frames = hits = filled = 0; }

void cryFrame(const int16_t* f, float noiseDb, uint32_t now) {
  hits += cryLike(f, noiseDb);
  if (++frames < 50) return;                       // score once per second
  // ponytail: ~70 % cry-like frames in a second counts as certain; speech rarely holds a voiced pitch that long
  float s = min(1.0f, hits / 50.0f / 0.7f);
  frames = hits = 0;
  uint8_t win = constrain(cal.cryMinS, 1, 10);
  if (head >= win) head = 0;                       // `cal set cryMinS` at runtime
  if (filled > win) filled = win;
  score[head] = s;
  head = (head + 1) % win;
  if (filled < win) filled++;
  float avg = 0;
  for (int i = 0; i < filled; i++) avg += score[i];
  avg = filled == win ? avg / win : 0;

  if (s >= 0.5f) { lastCryAt = now; if (episode) cryS += 1; }
  if (!episode) {
    if (avg >= cal.cryConf) {                      // sustained >= cryMinS: the parent alert (§4.8)
      episode = true;
      cryS = win;
      S.crying = true;
      nextEventAt = now + 60000;
      Serial.printf("[cry] crying %.0f s, confidence %.2f (heuristic, not for release)\n", cryS, avg);
      netEventCry(avg, cryS);
    }
  } else if (now - lastCryAt >= 20000) {           // 20 s without crying ends it
    episode = false;
    S.crying = false;
    Serial.printf("[cry] stopped after %.0f s\n", cryS);
  } else if ((int32_t)(now - nextEventAt) >= 0 && avg >= cal.cryConf) {   // again every 60 s while it continues
    nextEventAt = now + 60000;
    netEventCry(avg, cryS);
  }
}

bool crySelfTest(Print& out) {
  static int16_t f[N];
  float h[3];
  const float hz[2] = {400, 1500};                 // a cry's pitch; a whistle's
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < N; i++) f[i] = 8000 * sinf(TWO_PI * hz[k] * i / 16000);
    h[k] = harmonicity(f);
  }
  for (int i = 0; i < N; i++) f[i] = (int16_t)(esp_random() & 0x3FFF) - 0x2000;   // white noise
  h[2] = harmonicity(f);
  bool ok = h[0] >= 0.9f && h[1] == 0 && h[2] < 0.6f;
  out.printf("cry pitch check: 400 Hz %.2f, 1500 Hz %.2f, noise %.2f -> %s | episode %d\n", h[0], h[1], h[2],
             ok ? "PASS" : "FAIL", episode);
  return ok;
}
