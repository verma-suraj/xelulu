#include "Scs.h"

#define INST_PING 0x01
#define INST_READ 0x02
#define INST_WRITE 0x03
#define INST_SYNC_WRITE 0x83

static HardwareSerial& port = Serial1;
static bool echo = false, started = false;

static uint8_t checksum(const uint8_t* p, uint8_t n) {   // over id..last param
  uint8_t s = 0;
  for (uint8_t i = 0; i < n; i++) s += p[i];
  return ~s;
}

static uint8_t build(uint8_t* pkt, uint8_t id, uint8_t inst, const uint8_t* params, uint8_t np) {
  pkt[0] = 0xFF; pkt[1] = 0xFF; pkt[2] = id; pkt[3] = np + 2; pkt[4] = inst;
  memcpy(pkt + 5, params, np);
  pkt[5 + np] = checksum(pkt + 2, np + 3);
  return np + 6;
}

// Send a packet, drop our own echo, and collect `respLen` reply bytes.
// Bounded: <= 3 ms of busy-wait, never blocks the body task for longer.
static bool transact(const uint8_t* pkt, uint8_t n, uint8_t* resp, uint8_t respLen) {
  if (!started) return false;                 // bus off (USB console owns GPIO 19/20): every servo reads missing
  while (port.available()) port.read();
  port.write(pkt, n);
  port.flush();                               // until the last stop bit is out
  uint8_t want = (echo ? n : 0) + respLen, got = 0;
  uint8_t buf[80];
  uint32_t t0 = micros();
  while (got < want && micros() - t0 < 3000) {
    int c = port.read();
    if (c >= 0) buf[got++] = (uint8_t)c;
  }
  uint8_t off = (echo && got >= n && memcmp(buf, pkt, n) == 0) ? n : 0;
  if (!respLen) return true;
  if (got - off < respLen) return false;
  const uint8_t* r = buf + off;
  if (r[0] != 0xFF || r[1] != 0xFF || r[2] != pkt[2] || r[3] != respLen - 4) return false;
  if (r[respLen - 1] != checksum(r + 2, respLen - 3)) return false;
  memcpy(resp, r, respLen);
  return true;
}

bool scsBegin(int8_t tx, int8_t rx) {
  if (started) port.end();
  port.setRxBufferSize(256);
  port.begin(1000000, SERIAL_8N1, rx, tx);
  started = true;
  // Echo test: ping an ID nobody uses. If our own bytes come back, the line
  // is the 1 kOhm join; with a direction buffer (74HC125) they would not.
  uint8_t pkt[8];
  uint8_t n = build(pkt, 0xFD, INST_PING, nullptr, 0);
  while (port.available()) port.read();
  port.write(pkt, n);
  port.flush();
  uint8_t got = 0, buf[8];
  uint32_t t0 = micros();
  while (got < n && micros() - t0 < 1500) { int c = port.read(); if (c >= 0) buf[got++] = c; }
  echo = got == n && memcmp(buf, pkt, n) == 0;
  return true;
}

void scsEnd() { if (started) port.end(); started = false; }
bool scsEcho() { return echo; }

bool scsPing(uint8_t id) {
  uint8_t pkt[8], resp[6];
  uint8_t n = build(pkt, id, INST_PING, nullptr, 0);
  return transact(pkt, n, resp, 6);
}

bool scsRead(uint8_t id, uint8_t addr, uint8_t* buf, uint8_t len) {
  uint8_t params[2] = {addr, len}, pkt[10], resp[64];
  uint8_t n = build(pkt, id, INST_READ, params, 2);
  if (!transact(pkt, n, resp, len + 6)) return false;
  memcpy(buf, resp + 5, len);
  return true;
}

bool scsWrite(uint8_t id, uint8_t addr, const uint8_t* data, uint8_t len) {
  uint8_t params[32], pkt[40], resp[6];
  params[0] = addr;
  memcpy(params + 1, data, len);
  uint8_t n = build(pkt, id, INST_WRITE, params, len + 1);
  return transact(pkt, n, resp, 6);
}

void scsSyncWrite(uint8_t addr, uint8_t len, const uint8_t* ids, uint8_t count, const uint8_t* data) {
  uint8_t params[64], pkt[72];
  uint8_t np = 2;
  params[0] = addr; params[1] = len;
  for (uint8_t i = 0; i < count; i++) {
    params[np++] = ids[i];
    memcpy(params + np, data + i * len, len);
    np += len;
  }
  uint8_t n = build(pkt, 0xFE, INST_SYNC_WRITE, params, np);   // broadcast: no reply
  transact(pkt, n, nullptr, 0);
}

bool scsFeedback(uint8_t id, ScsFeedback& fb) {
  uint8_t b[8];
  if (!scsRead(id, SCS_PRESENT, b, 8)) return false;
  fb.pos = b[0] << 8 | b[1];
  uint16_t sp = b[2] << 8 | b[3], ld = b[4] << 8 | b[5];
  fb.speed = (sp & 0x8000) ? -(int16_t)(sp & 0x7FFF) : (int16_t)sp;
  fb.load = (ld & 0x0400) ? -(int16_t)(ld & 0x03FF) : (int16_t)(ld & 0x03FF);   // bit 10 = direction
  fb.volt10 = b[6];
  fb.temp = b[7];
  return true;
}

bool scsEepromWord(uint8_t id, uint8_t addr, uint16_t value) {
  uint8_t unlock = 0, lock = 1, v[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  bool ok = scsWrite(id, SCS_LOCK, &unlock, 1);
  ok = scsWrite(id, addr, v, 2) && ok;
  scsWrite(id, SCS_LOCK, &lock, 1);
  return ok;
}

bool scsSetId(uint8_t id, uint8_t newId) {
  uint8_t unlock = 0, lock = 1;
  if (!scsWrite(id, SCS_LOCK, &unlock, 1)) return false;
  scsWrite(id, SCS_ID, &newId, 1);           // the reply may come from the new ID: don't trust it
  delay(5);
  scsWrite(newId, SCS_LOCK, &lock, 1);
  return scsPing(newId);
}
