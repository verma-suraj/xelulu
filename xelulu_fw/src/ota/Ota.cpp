#include "Ota.h"
#include "../net/Net.h"
#include "../State.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/sha256.h"
#include <ArduinoJson.h>

// The core marks every new image valid at boot unless this says "later".
// Later = after hello + the first status (§13.4 step 5); a new image that
// never gets there is rolled back by the bootloader on its next reset.
bool verifyRollbackLater() { return true; }

static bool pendingVerify = false;
static volatile bool running = false, localBusy = false;
static char cmdId[64], manifestUrl[256];

// A Wi-Fi upload marks RTC memory, which survives its reboot into the new image. That image is
// kept once Wi-Fi works (hello may never come: the stack can be off while someone uploads), and
// rolls back on the next reset if it cannot even join Wi-Fi.
#define LOCAL_MAGIC 0x10CA10CAu
RTC_NOINIT_ATTR static uint32_t rtcLocal;
static bool localImage = false;

void otaBegin() {
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    pendingVerify = true;
    localImage = rtcLocal == LOCAL_MAGIC;
    Serial.println(localImage ? "[ota] new image from a Wi-Fi upload: kept once Wi-Fi works, 30 s after boot"
                              : "[ota] new image: kept only if hello + status succeed before the next reset");
  }
  rtcLocal = 0;
}

void otaLocal(bool busy) {
  rtcLocal = busy ? LOCAL_MAGIC : 0;
  localBusy = busy;
}

void otaLocalCheck(uint32_t now) {
  if (localImage && S.wifiUp && now > 30000) { localImage = false; otaMarkValid(); }
}

void otaMarkValid() {
  if (!pendingVerify) return;
  esp_ota_mark_app_valid_cancel_rollback();
  pendingVerify = false;
  Serial.println("[ota] image marked valid");
}

bool otaRunning() { return running || localBusy; }

static esp_http_client_handle_t httpOpen(const char* url, int64_t& len) {
  esp_http_client_config_t hc = {};
  hc.url = url;
  hc.crt_bundle_attach = esp_crt_bundle_attach;   // https only, verified against the bundle
  hc.timeout_ms = 15000;
  hc.buffer_size = 2048;
  esp_http_client_handle_t c = esp_http_client_init(&hc);
  if (!c) return nullptr;
  if (esp_http_client_open(c, 0) != ESP_OK) { esp_http_client_cleanup(c); return nullptr; }
  len = esp_http_client_fetch_headers(c);
  if (esp_http_client_get_status_code(c) != 200) { esp_http_client_close(c); esp_http_client_cleanup(c); return nullptr; }
  return c;
}

static void httpClose(esp_http_client_handle_t c) { esp_http_client_close(c); esp_http_client_cleanup(c); }

static const char* run() {
  // 1. manifest (interim: version, url, sha256, size)
  int64_t len;
  esp_http_client_handle_t c = httpOpen(manifestUrl, len);
  if (!c) return "manifest fetch failed";
  static char body[2048];
  int n = esp_http_client_read(c, body, sizeof body - 1);
  httpClose(c);
  if (n <= 0) return "empty manifest";
  body[n] = 0;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "manifest is not JSON";
  const char* url = doc["url"] | "";
  const char* sha = doc["sha256"] | "";
  uint32_t size = doc["size"] | 0;
  if (strncmp(url, "https://", 8) || strlen(sha) != 64 || !size) return "manifest fields";
  Serial.printf("[ota] version %s, %lu bytes\n", (const char*)(doc["version"] | "?"), (unsigned long)size);

  // 2. stream into the inactive slot, hashing every byte
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (!part || size > part->size) return "image does not fit";
  c = httpOpen(url, len);
  if (!c) return "image fetch failed";
  esp_ota_handle_t h;
  if (esp_ota_begin(part, size, &h) != ESP_OK) { httpClose(c); return "ota begin"; }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  static uint8_t buf[4096];
  uint32_t got = 0;
  while ((n = esp_http_client_read(c, (char*)buf, sizeof buf)) > 0) {
    mbedtls_sha256_update(&ctx, buf, n);
    if (esp_ota_write(h, buf, n) != ESP_OK) break;
    got += n;
  }
  httpClose(c);
  uint8_t digest[32];
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  if (got != size) { esp_ota_abort(h); return "short download"; }
  if (esp_ota_end(h) != ESP_OK) return "image invalid";
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
  if (strcasecmp(hex, sha)) return "sha256 mismatch";
  if (esp_ota_set_boot_partition(part) != ESP_OK) return "set boot partition";
  return nullptr;
}

static void otaTask(void*) {
  const char* err = run();
  if (err) {
    Serial.printf("[ota] failed: %s\n", err);
    netAck(cmdId, ACK_FAILED);
    running = false;
    vTaskDelete(nullptr);
  }
  Serial.println("[ota] verified and staged: acking done, rebooting");
  netAck(cmdId, ACK_DONE);              // done = staged, before the reboot (§4.7)
  delay(2000);                          // let the ack leave
  esp_restart();
}

bool otaStart(const char* id, const char* url) {
  if (running || localBusy) return false;
  running = true;
  rtcLocal = 0;                         // this image comes from the cloud: hello + status decide
  strlcpy(cmdId, id, sizeof cmdId);
  strlcpy(manifestUrl, url, sizeof manifestUrl);
  xTaskCreatePinnedToCore(otaTask, "ota", 8192, nullptr, 3, nullptr, 0);
  return true;
}
