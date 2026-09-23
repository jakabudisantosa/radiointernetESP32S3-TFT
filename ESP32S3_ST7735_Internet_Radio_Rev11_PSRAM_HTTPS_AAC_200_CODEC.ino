/*
  ESP32-S3 SUPERMINI INTERNET RADIO - REV11 HTTPS IMPROVED
  ST7735 128x160 + MAX98357A + NTP + ALARM + CAPTIVE PORTAL

  ST7735:
    SCLK -> GPIO12
    MOSI -> GPIO11
    DC   -> GPIO10
    RST  -> GPIO9
    CS   -> GPIO8
    BL   -> GPIO13
  MAX98357A:
    BCLK -> GPIO5
    LRC  -> GPIO6
    DIN  -> GPIO7

  BUTTON RADIO/VOLUME:
    GPIO1 -> PUSH BUTTON -> GND

  BUTTON MODE:
    GPIO2 -> PUSH BUTTON -> GND

  AUDIO:
    HTTP  -> AudioFileSourceICYStream
    HTTPS -> built-in secure streaming source (WiFiClientSecure)
    MP3   -> AudioGeneratorMP3
    AAC   -> AudioGeneratorAAC
    Buffer -> HTTP 256 KB internal, HTTPS 512 KB PSRAM (jika tersedia)

  Note:
    HTTPS uses setInsecure() for broad radio-server compatibility.
    This enables TLS without requiring a CA certificate.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "esp_heap_caps.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include "AudioFileSource.h"
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "AudioGeneratorMP3.h"
#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"

// ============================================================
// WIFI
// ============================================================
const char* WIFI_SSID = "Dipta Damar";
const char* WIFI_PASS = "94118FA5598";

// ============================================================
// ST7735
// ============================================================
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC   10
#define TFT_RST  9
#define TFT_CS   8
#define TFT_BL   13

// BACKLIGHT: RADIO = selalu ON, NTP = ON 1 menit setelah aktivitas/tombol
#define NTP_BL_TIMEOUT  (1UL * 60UL * 1000UL)
bool backlightOn = true;
unsigned long backlightTimer = 0;

Adafruit_ST7735 tft(TFT_CS,TFT_DC,TFT_RST);

// ============================================================
// MAX98357A
// ============================================================
#define I2S_BCLK 5
#define I2S_LRC  6
#define I2S_DOUT 7

// ============================================================
// BUTTONS
// ============================================================
#define BUTTON_RADIO 1
#define BUTTON_MODE  2

// ============================================================
// RADIO DATABASE - LITTLEFS /radios.json
// ============================================================
#define MAX_STATIONS 200

struct RadioStation {
  String name;
  String url;
  String codec;   // "MP3" atau "AAC"; default MP3
};

RadioStation stations[MAX_STATIONS];
int stationCount = 0;

// Global station/alarm indexes must be declared before functions that use them.
int currentStation = 1;
int alarmStation = 3;

// Data bawaan Rev3 hanya dipakai SATU KALI untuk membuat radios.json
// pada boot pertama. Setelah /radios.json ada, daftar radio sepenuhnya
// dibaca dari LittleFS dan dapat diubah melalui Web Radio Manager.
// ============================================================
// DEFAULT RADIO
// URUT BERDASARKAN NEGARA ASAL
// Nama dan URL tetap berpasangan
// ============================================================

const char* DEFAULT_RADIO_NAMES[] = {
  // ==========================================================
  // 1. INDONESIA
  // ==========================================================
  "Radio Dangdut",
  "Elshinta FM",
  "88.4 FM OKEZONE",
  "Retjo Buntung",
  "RADIONESIA Indonesia",
  "El Shaddai FM Solo",
  "Hitz FM Bandung",
  "V Radio Indonesia",
  "Radio Hang FM",
  "Suara Giri FM",
  "Trijaya 104.6FM",
  "Suara Muslim Surabaya",
  "Andika FM Kediri",
  "Sonora FM Palembang",
  "Unisi Radio",
  "Radio Bantul 89.10 FM",
  "107 Dakta FM",
  "Prosalina FM",
  "Diozz FM",
  "Anime FM7",
  "Kawaii Music",
  "Campur sari Jakarta",
  "Elgangga FM",
  "Megaswara FM Bogor",
  "Gajahmada FM Semarang",
  // ==========================================================
  // 2. JAPAN
  // ==========================================================
  "Jazz Sakura Japan",
  "City Pop 3",
  "J-POP Powerplay",
  "FM Kahoku",
  "Shonan Beach FM 78.9",
  "J-Club Club Bandstand",
  "Nonstop Casiopea",
  "Asia DREAM Radio",
  "Yumi Co Radio",
  "Kawaii Anime Radio",
  "Fred Film Radio",

  // ==========================================================
  // 3. SOUTH KOREA
  // ==========================================================
  "JeonjuFM",
  "Korean City Pop",

  // ==========================================================
  // 4. AUSTRALIA
  // ==========================================================
  "1116 SEN Melbourne",

  // ==========================================================
  // 5. AUSTRIA
  // ==========================================================
  "JazzW3",

  // ==========================================================
  // 6. CZECH REPUBLIC
  // ==========================================================
  "Oldies Radio",
  "Radio 4ever",

  // ==========================================================
  // 7. FRANCE
  // ==========================================================
  "Boombox Radio",
  "Jazz Radio Funk",

  // ==========================================================
  // 8. GERMANY
  // ==========================================================
  "Fresh80s",
  "Hit Radio FFH",
  "Antenne Bayern",
  "90s90s Dance",
  "Oldies von 1A Radio",
  "Oldies on Radio",
  "Oldies but Goldies",

  // ==========================================================
  // 9. HUNGARY
  // ==========================================================
  "Tilos Radio 128",

  // ==========================================================
  // 10. ITALY
  // ==========================================================
  "Choice Stereo Italia",
  "Radio Madeo",
  "Celodex Disco Club",

  // ==========================================================
  // 11. NETHERLANDS
  // ==========================================================
  "Radio De Schans",
  "Radio 10 80's Hits",
  "Concertzender Geen dag zonder Bach",

  // ==========================================================
  // 12. NORWAY
  // ==========================================================
  "Arctic Outpost AM1270",
  "Radio Vinyl Oslo",

  // ==========================================================
  // 13. RUSSIA
  // ==========================================================
  "ROCK FM Rusia",

  // ==========================================================
  // 14. UNITED KINGDOM
  // ==========================================================
  "Classic FM UK",
  "Radio X UK",
  "Heart London",
  "Capital XTRA",
  "Vibe 107.6",
  "181.FM UK Top 40",
  "181.FM Power 181",
  "BBC",
  "The UK 1940s Radio Station",
  "Smooth 80s",
  "Easy 50s",
  "Treehouse Radio",
  "Radio Jackie",
  "Love 80s",
  "Alive Radio 107.3",
  // ==========================================================
  // 15. UNITED STATES
  // ==========================================================
  "KFJC 128k",
  "WBRH MP3",
  "The Big Band Era",
  "HOG STORY",
  "KCEA Big Band",
  "101 SMOOTH JAZZ",
  "SomaFM - Secret Agent",
  "SomaFM - Lush",
  "Groove Salad",
  "Indie Pop Rocks!",
  "Underground 80s",
  "Metal Detector",
  "Illinois Street Lounge",
  "SomaFM Drone Zone",
  "SomaFM Beat Blender",
  "SomaFM The Trip",
  "SomaFM Folk Forward",
  "Space Station Soma",
  "Left Coast 70s",
  "Public Domain Jazz Swing",
  "Radio Artifact",
  "Radio Paradise",
  "Radio Paradise DC",
  "The Big 80s Station",
  "977 80s80s",
  "113.fm Hits 1983",
  "Scott Shannon's True Oldies Channel"
};


// ============================================================
// URL SESUAI URUTAN NAMA DI ATAS
// ============================================================

const char* DEFAULT_RADIO_URLS[] = {

  // ==========================================================
  // 1. INDONESIA
  // ==========================================================
  "http://202.147.199.99:8000/",
  "https://stream-ssl.arenastreaming.com:8000/jakarta",
  "http://202.147.199.98/;",
  "http://45.64.97.82:9940/",
  "http://194.233.74.75:9100/stream",
  "http://shaddai.onlivestreaming.net:9130/live",
  "http://hits.unikom.ac.id:9996/",
  "http://202.147.199.100:8000/;stream.nsv",
  "http://185.47.62.52:8000/",
  "http://streaming.girifm.com:8010/",
  "http://202.147.199.101:8000/;stream.nsv",
  "http://pu.klikhost.com:8052/",
  "http://andikafm.onlivestreaming.net:1057/stream",
  "https://mediacp-sg2.arenastreaming.com:8004/stream",
  "http://studio1.indostreamers.com:8002/stream",
  "https://stream.swadesifm.com/radio/8130/radio.mp3",
  "http://175.103.48.4:9302/stream",
  "https://i.klikhost.com/8618/stream?1708788698312",
  "https://stream-ssl.arenastreaming.com:8046/live",
  "https://animefm.stream.laut.fm/animefm",
  "https://kawaii-music.stream.laut.fm/kawaii-music",
  "https://a8.siar.us/listen/campursari/stream",
  "https://b.alhastream.com:5140/radio",
  "https://server.adyadigitalteknologi.com:8010/1",
  "https://server.radioimeldafm.co.id/radio/8040/gajahmadafm",
  // ==========================================================
  // 2. JAPAN
  // ==========================================================
  "http://kathy.torontocast.com:3330/stream/1/?esPlayer&cb=82181.mp3",
  "http://65.21.61.215:8000/citypopthree",
  "https://kathy.torontocast.com:3560/",
  "http://radio.kahoku.net:8000/",
  "http://shonanbeachfm.out.airtime.pro:8000/shonanbeachfm_a",
  "http://cast1.torontocast.com:2060/;.mp3",
  "http://hyades.shoutca.st:8551/;",
  "http://quincy.torontocast.com:2020/stream.mp3",
  "http://s1.yumicoradio.net:8000/stream_128",
  "http://158.69.227.214:8137/stream",
  "https://s10.webradio-hosting.com/proxy/fredradiojp/stream",

  // ==========================================================
  // 3. SOUTH KOREA
  // ==========================================================
  "http://radiostream.communityradio.kr/jcfm1",
  "http://65.21.61.215:8000/citypoptwo",

  // ==========================================================
  // 4. AUSTRALIA
  // ==========================================================
  "http://13.54.221.214:8000/sen.mp3",

  // ==========================================================
  // 5. AUSTRIA
  // ==========================================================
  "http://jazz.w3.at:8000/w3jazz.mp3",

  // ==========================================================
  // 6. CZECH REPUBLIC
  // ==========================================================
  "http://ice.abradio.cz/oldiesradio128.mp3",
  "https://pragustream.live:8020/radio.mp3",

  // ==========================================================
  // 7. FRANCE
  // ==========================================================
  "http://hosting.radiomedia.fr:1100/boombox",
  "http://jazz-wr06.ice.infomaniak.ch/jazz-wr06-128.mp3",


  // ==========================================================
  // 8. GERMANY
  // ==========================================================
  "http://s37.derstream.net:80/128.mp3",
  "http://mp3.ffh.de/radioffh/hqlivestream.mp3",
  "http://mp3channels.webradio.antenne.de/antenne",
  "http://streams.90s90s.de/eurodance/mp3-128/radiode/",
  "https://1a-oldies.radionetz.de/1a-oldies.mp3",
  "https://0n-oldies.radionetz.de/0n-oldies.mp3",
  "http://mp3channels.webradio.antenne.de/oldies-but-goldies",
  // ==========================================================
  // 9. HUNGARY
  // ==========================================================
  "http://stream.tilos.hu/tilos_128.mp3",

  // ==========================================================
  // 10. ITALY
  // ==========================================================
  "http://pstnet12.shoutcastnet.com:10140/stream",
  "http://sr14.inmystream.it:8300/stream",
  "http://radio.celodex.com:8000/disco.mp3",

  // ==========================================================
  // 11. NETHERLANDS
  // ==========================================================
  "http://mediacp.audiostreamen.nl:8032/radiodeschans.mp3",
  "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR20.mp3",
  "http://streams.greenhost.nl:8080/bach",

  // ==========================================================
  // 12. NORWAY
  // ==========================================================
  "http://radio.streemlion.com:3470/stream",
  "http://live-bauerno.sharp-stream.com/vinyl_no_mp3",

  // ==========================================================
  // 13. RUSSIA
  // ==========================================================
  "http://nashe1.hostingradio.ru/rock-128.mp3",

  // ==========================================================
  // 14. UNITED KINGDOM
  // ==========================================================
  "http://ice-the.musicradio.com/ClassicFMMP3",
  "http://icecast.thisisdax.com/RadioXUKMP3",
  "http://ice-sov.musicradio.com/HeartLondonMP3",
  "http://media-the.musicradio.com/CapitalXTRALondonMP3",
  "http://stream.cotswoldgrp.com:8001/main",
  "http://listen.181fm.com/181-uktop40_128k.mp3",
  "http://listen.181fm.com/181-power_128k.mp3",
  "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service",
  "http://solid2.streamupsolutions.com:24929/stream",
  "https://media-ice.musicradio.com/Smooth80sMP3",
  "https://streaming.exclusive.radio/uber/easy50/icecast.audio",
  "http://s6.autopo.st:8515/live",
  "http://media.radiojackie.com:12614/",
  "http://stream1.themediasite.co.uk:8038/stream",
  "http://stream2.hippynet.co.uk:8013/;",
  // ==========================================================
  // 15. UNITED STATES
  // ==========================================================
  "http://netcast.kfjc.org/kfjc-128k-mp3",
  "http://wbrh.streamguys1.com:80/wbrh-mp3",
  "http://cheetah.streemlion.com:4460/stream",
  "http://stream.hogstory.net:8000/stream",
  "http://streaming.rubinbroadcasting.com/kcea",
  "http://jking.cdnstream1.com/b22139_128mp3",

  "http://ice1.somafm.com/secretagent-128-mp3",
  "http://ice1.somafm.com/lush-128-mp3",
  "http://ice1.somafm.com/groovesalad-128-mp3",
  "http://ice1.somafm.com/indiepop-128-mp3",
  "http://ice1.somafm.com/u80s-128-mp3",
  "http://ice1.somafm.com/metal-128-mp3",
  "http://ice1.somafm.com/illstreet-128-mp3",
  "http://ice1.somafm.com/dronezone-128-mp3",
  "http://ice1.somafm.com/beatblender-128-mp3",
  "http://ice1.somafm.com/thetrip-128-mp3",
  "http://ice1.somafm.com/folkfwd-128-mp3",
  "http://ice1.somafm.com/spacestation-128-mp3",
  "https://ice5.somafm.com/seventies-128-mp3",
  "http://relay.publicdomainradio.org/jazz_swing.mp3",
  "http://stream.cinradio.org/wvxuhd2.mp3",
  "http://stream-tx1.radioparadise.com/mp3-128",
  "http://stream-dc1.radioparadise.com/mp3-128",
  "http://158.69.114.190:8065/;",
  "http://26433.live.streamtheworld.com/977_80_SC",
  "http://113fm-atunwadigital.streamguys1.com:80/1027",
  "http://streaming.live365.com/b92108_128mp3"
};

const size_t DEFAULT_stationCount =
  sizeof(DEFAULT_RADIO_NAMES) / sizeof(DEFAULT_RADIO_NAMES[0]);

bool radioFSReady = false;

bool saveRadioDatabase() {
  if (!radioFSReady) return false;

  File f = LittleFS.open("/radios.json", "w");
  if (!f) {
    Serial.println("[RADIO DB] Gagal membuka /radios.json untuk tulis");
    return false;
  }

  DynamicJsonDocument doc(96 * 1024);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < stationCount; i++) {
    JsonObject o = arr.createNestedObject();
    o["name"]  = stations[i].name;
    o["url"]   = stations[i].url;
    o["codec"] = stations[i].codec.length() ? stations[i].codec : "MP3";
  }

  bool ok = serializeJson(doc, f) > 0;
  f.close();

  if (ok) Serial.printf("[RADIO DB] Tersimpan: %d radio\n", stationCount);
  else Serial.println("[RADIO DB] Gagal menulis JSON");

  return ok;
}

bool loadRadioDatabase() {
  if (!radioFSReady || !LittleFS.exists("/radios.json"))
    return false;

  File f = LittleFS.open("/radios.json", "r");
  if (!f) return false;

  DynamicJsonDocument doc(96 * 1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print("[RADIO DB] JSON rusak: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  stationCount = 0;
  bool needsCodecMigration = false;

  for (JsonObject o : arr) {
    if (stationCount >= MAX_STATIONS) break;

    String name  = o["name"]  | "";
    String url   = o["url"]   | "";
    if (!o.containsKey("codec")) needsCodecMigration = true;
    String codec = o["codec"] | "MP3";   // database lama otomatis MP3

    name.trim();
    url.trim();
    codec.trim();
    codec.toUpperCase();

    if (codec != "AAC" && codec != "MP3") codec = "MP3";
    if (!name.length() || !url.length()) continue;

    stations[stationCount].name  = name;
    stations[stationCount].url   = url;
    stations[stationCount].codec = codec;
    stationCount++;
  }

  if (needsCodecMigration && stationCount > 0) {
    Serial.println("[RADIO DB] Database lama: codec tidak ada -> semua dianggap MP3");
    saveRadioDatabase();
    Serial.println("[RADIO DB] Codec MP3 ditambahkan ke /radios.json");
  }

  Serial.printf("[RADIO DB] Dibaca: %d radio\n", stationCount);
  return stationCount > 0;
}

void createDefaultRadioDatabase() {
  stationCount = 0;

  size_t n = DEFAULT_stationCount;
  if (sizeof(DEFAULT_RADIO_URLS) / sizeof(DEFAULT_RADIO_URLS[0]) < n)
    n = sizeof(DEFAULT_RADIO_URLS) / sizeof(DEFAULT_RADIO_URLS[0]);

  if (n > MAX_STATIONS) n = MAX_STATIONS;

  for (size_t i = 0; i < n; i++) {
    stations[stationCount].name  = DEFAULT_RADIO_NAMES[i];
    stations[stationCount].url   = DEFAULT_RADIO_URLS[i];
    stations[stationCount].codec = "MP3";
    stationCount++;
  }

  saveRadioDatabase();
  Serial.printf("[RADIO DB] Database awal dibuat: %d radio\n", stationCount);
}

void initRadioDatabase() {
  radioFSReady = LittleFS.begin(true);

  if (!radioFSReady) {
    Serial.println("[RADIO DB] LittleFS gagal mount");
    stationCount = 0;
    return;
  }

  if (!loadRadioDatabase()) {
    Serial.println("[RADIO DB] /radios.json belum ada -> membuat dari data Rev11");
    createDefaultRadioDatabase();
  }
}

void normalizeStationIndex() {
  if (stationCount <= 0) {
    currentStation = -1;
    alarmStation = -1;
    return;
  }

  if (currentStation < 0 || currentStation >= stationCount)
    currentStation = 0;

  if (alarmStation < 0 || alarmStation >= stationCount)
    alarmStation = 0;
}

// ============================================================
// HTTPS AUDIO SOURCE FOR ESP32-S3
// ============================================================
// ESP8266Audio's normal ICY source is retained for HTTP, exactly as in
// Rev9. HTTPS is handled by this source so HTTP and HTTPS can coexist.
//
// Icy-MetaData is explicitly disabled for HTTPS. This prevents ICY metadata
// blocks from being inserted into the MP3 byte stream. The station title
// metadata is therefore not read on the HTTPS path, but audio playback is
// kept clean and compatible with MP3 decoders.
//
// TLS certificate verification is intentionally disabled (setInsecure()).
// This is useful for public radio servers with changing/missing CA chains.
// It means HTTPS encryption is used, but the server certificate is not
// authenticated. Do not use this mode for private/authenticated services.

class AudioFileSourceHTTPSStream : public AudioFileSource {
public:
  AudioFileSourceHTTPSStream() {
    pos = 0;
    size = -1;
    opened = false;
  }

  explicit AudioFileSourceHTTPSStream(const char* url) {
    pos = 0;
    size = -1;
    opened = false;
    open(url);
  }

  ~AudioFileSourceHTTPSStream() override {
    close();
  }

  bool open(const char* url) override {
    close();

    if (!url || strncmp(url, "https://", 8) != 0)
      return false;

    pos = 0;
    size = -1;

    // HTTPS tetap tanpa verifikasi CA agar kompatibel dengan radio publik.
    client.setInsecure();

    // Timeout socket sedikit lebih longgar untuk radio streaming.
    client.setTimeout(5000);

    // HTTP/1.0 dipertahankan agar body stream dapat dibaca langsung.
    http.useHTTP10(true);
    http.setTimeout(15000);
    http.setReuse(false);

    // Matikan ICY metadata agar blok metadata tidak masuk ke MP3.
    http.addHeader("Icy-MetaData", "0");
    http.addHeader("User-Agent", "ESP32-S3-Internet-Radio-Rev11");
    http.addHeader("Accept", "audio/mpeg,audio/*,*/*;q=0.8");
    http.addHeader("Connection", "close");

    if (!http.begin(client, url)) {
      Serial.println("[HTTPS] HTTPClient.begin FAILED");
      return false;
    }

    int code = http.GET();

    if (code != HTTP_CODE_OK) {
      Serial.print("[HTTPS] HTTP status: ");
      Serial.println(code);
      http.end();
      return false;
    }

    size = http.getSize();
    opened = true;

    Serial.print("[HTTPS] TLS stream OPEN, size=");
    Serial.println(size);

    Serial.print("[HTTPS] Content-Type: ");
    Serial.println(http.header("Content-Type"));

    return true;
  }

  uint32_t read(void* data, uint32_t len) override {
    return readInternal(data, len, false);
  }

  uint32_t readNonBlock(void* data, uint32_t len) override {
    return readInternal(data, len, true);
  }

  bool seek(int32_t, int) override {
    return false;
  }

  bool close() override {
    if (opened || http.connected())
      http.end();

    opened = false;
    pos = 0;
    size = -1;

    return true;
  }

  bool isOpen() override {
    return opened && http.connected();
  }

  uint32_t getSize() override {
    return (size > 0) ? (uint32_t)size : 0;
  }

  uint32_t getPos() override {
    return (uint32_t)pos;
  }

  bool loop() override {
    return isOpen();
  }

private:
  /*
     HTTPS feeding strategy:

     Rev sebelumnya terlalu cepat mengembalikan 0 ketika available()
     sementara kecil/0. Pada bitrate tinggi hal ini dapat membuat
     AudioFileSourceBuffer mengalami underrun.

     Sekarang:
     - readNonBlock() tetap non-blocking secara ringan, tetapi memberi
       waktu sampai 120 ms agar TLS packet berikutnya datang.
     - Pembacaan dibatasi 8 KB sekali jalan agar tidak menahan decoder
       terlalu lama.
     - readBytes() memakai char* yang kompatibel dengan Stream API ESP32.
     - read() blocking menunggu data sampai 1500 ms.
  */

  uint32_t readInternal(void* data, uint32_t len, bool nonBlock) {
    if (!opened || !data || len == 0)
      return 0;

    Stream* net = http.getStreamPtr();

    if (!net)
      return 0;

    // Jangan melakukan satu read yang terlalu besar.
    // 8 KB cukup besar untuk TLS/radio tetapi tetap ramah decoder.
    const uint32_t MAX_READ_CHUNK = 8192;

    if (len > MAX_READ_CHUNK)
      len = MAX_READ_CHUNK;

    if (nonBlock) {
      /*
         Beri sedikit waktu untuk TLS layer menerima packet berikutnya.
         Ini bukan delay panjang: maksimum 120 ms.
      */
      unsigned long startWait = millis();

      while (net->available() <= 0) {
        if (!http.connected())
          return 0;

        if (millis() - startWait >= 120)
          return 0;

        delay(2);
        yield();
      }

      int avail = net->available();

      if (avail <= 0)
        return 0;

      if ((uint32_t)avail < len)
        len = (uint32_t)avail;

      /*
         Stream::read() overload pada beberapa versi ESP32 core dapat
         berbeda. readBytes(char*, size_t) lebih kompatibel.
      */
      size_t n = net->readBytes((char*)data, len);

      if (n > 0) {
        pos += (int)n;
        return (uint32_t)n;
      }

      return 0;
    }

    /*
       Blocking read:
       tunggu data masuk terlebih dahulu, lalu ambil satu blok.
       Ini mencegah decoder menerima banyak return-0 hanya karena
       packet HTTPS belum selesai datang.
    */
    unsigned long startWait = millis();

    while (net->available() <= 0) {
      if (!http.connected())
        return 0;

      if (millis() - startWait >= 1500)
        return 0;

      delay(2);
      yield();
    }

    int avail = net->available();

    if (avail <= 0)
      return 0;

    if ((uint32_t)avail < len)
      len = (uint32_t)avail;

    size_t n = net->readBytes((char*)data, len);

    if (n > 0) {
      pos += (int)n;
      return (uint32_t)n;
    }

    return 0;
  }

  WiFiClientSecure client;
  HTTPClient http;

  int pos;
  int size;
  bool opened;
};

// ============================================================
// PSRAM AUDIO BUFFER - HTTPS ONLY
// ============================================================
// HTTP tetap memakai AudioFileSourceBuffer Rev9/Rev11.
// HTTPS memakai ring buffer di PSRAM agar RAM internal dapat dipakai
// oleh WiFi/TLS, MP3 decoder, I2S, TFT, dan WebServer.
//
// Ukuran 512 KB = ~16 detik data mentah pada 256 kbps.
// Ini hanya membantu menghadapi burst/jeda penerimaan HTTPS;
// PSRAM tidak menambah bandwidth WiFi.

class PSRAMAudioFileSourceBuffer : public AudioFileSource {
public:
  PSRAMAudioFileSourceBuffer(AudioFileSource* src, uint32_t capacity)
    : source(src), buff(nullptr), buffSize(capacity),
      head(0), tail(0), filled(0), totalRead(0), opened(false) {

    if (!source || capacity == 0)
      return;

    buff = (uint8_t*)heap_caps_malloc(
      capacity,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!buff) {
      Serial.printf("[PSRAM BUFFER] Allocation FAILED: %lu bytes\n",
                    (unsigned long)capacity);
      return;
    }

    opened = true;

    Serial.printf("[PSRAM BUFFER] Allocated: %lu bytes\n",
                  (unsigned long)capacity);
    Serial.printf("[PSRAM BUFFER] Free PSRAM after allocation: %u bytes\n",
                  ESP.getFreePsram());
  }

  ~PSRAMAudioFileSourceBuffer() override {
    close();
  }

  bool open(const char*) override {
    return opened;
  }

  uint32_t read(void* data, uint32_t len) override {
    if (!opened || !data || len == 0)
      return 0;

    // Untuk read blocking, isi buffer sampai ada data atau source berhenti.
    fillFromSource(true, len);

    return pop(data, len);
  }

  uint32_t readNonBlock(void* data, uint32_t len) override {
    if (!opened || !data || len == 0)
      return 0;

    // Non-blocking: ambil data yang tersedia, lalu coba isi sekali/bertahap.
    fillFromSource(false, len);

    return pop(data, len);
  }

  bool seek(int32_t, int) override {
    // Radio live stream tidak membutuhkan seek.
    return false;
  }

  bool close() override {
    opened = false;

    if (buff) {
      heap_caps_free(buff);
      buff = nullptr;
    }

    head = tail = filled = 0;
    return true;
  }

  bool isOpen() override {
    return opened && source && source->isOpen();
  }

  uint32_t getSize() override {
    if (!source)
      return 0;
    return source->getSize();
  }

  uint32_t getPos() override {
    return totalRead;
  }

  bool loop() override {
    return isOpen();
  }

  uint32_t availableBytes() const {
    return filled;
  }

private:
  void fillFromSource(bool blocking, uint32_t wanted) {
    if (!opened || !source || !source->isOpen() || !buff)
      return;

    // Jangan mengisi lebih dari setengah buffer dalam satu pemanggilan.
    // Ini menjaga CPU tetap kembali ke decoder/WiFi dengan cepat.
    uint32_t target = wanted;
    if (target < 8192)
      target = 8192;
    if (target > buffSize / 2)
      target = buffSize / 2;

    uint8_t attempts = 0;

    while (filled < target && filled < buffSize && attempts < 8) {
      uint32_t freeSpace = buffSize - filled;
      if (freeSpace == 0)
        break;

      uint32_t contiguous = buffSize - head;
      uint32_t request = contiguous;

      if (request > freeSpace)
        request = freeSpace;
      if (request > 8192)
        request = 8192;

      uint32_t got = blocking
        ? source->read(buff + head, request)
        : source->readNonBlock(buff + head, request);

      if (got == 0)
        break;

      head = (head + got) % buffSize;
      filled += got;
      attempts++;

      if (!blocking)
        break;
    }
  }

  uint32_t pop(void* data, uint32_t len) {
    if (!data || len == 0 || filled == 0)
      return 0;

    if (len > filled)
      len = filled;

    uint8_t* dst = (uint8_t*)data;
    uint32_t first = buffSize - tail;

    if (first > len)
      first = len;

    memcpy(dst, buff + tail, first);

    if (len > first)
      memcpy(dst + first, buff, len - first);

    tail = (tail + len) % buffSize;
    filled -= len;
    totalRead += len;

    return len;
  }

  AudioFileSource* source;
  uint8_t* buff;
  uint32_t buffSize;
  uint32_t head;
  uint32_t tail;
  uint32_t filled;
  uint32_t totalRead;
  bool opened;
};

// ============================================================
// AUDIO
// ============================================================
AudioGenerator* decoder = nullptr;
AudioFileSource* stream = nullptr;
AudioFileSourceBuffer* buffer = nullptr;
PSRAMAudioFileSourceBuffer* psramBuffer = nullptr;
AudioOutputI2S* out = nullptr;

// HTTP tetap 256 KB di jalur Rev9/Rev11.
// HTTPS memakai 512 KB di PSRAM bila PSRAM tersedia.
#define AUDIO_BUFFER_SIZE        (256 * 1024)
#define HTTPS_PSRAM_BUFFER_SIZE  (512 * 1024)

const unsigned long BUFFER_WARMUP_TIME = 4000;
const unsigned long RECONNECT_DELAY = 1500;
const uint8_t MAX_RECONNECT_ATTEMPTS = 3;

bool bufferWarming = false;
unsigned long radioStartTime = 0;
bool reconnectPending = false;
unsigned long reconnectTime = 0;

// Proteksi terhadap station yang URL-nya tidak valid / tidak mengirim stream.
// Maksimum satu kali reconnect otomatis agar Web Radio Manager tetap responsif.
uint8_t reconnectAttempts = 0;

// ============================================================
// NVS
// ============================================================
Preferences prefRadio;
Preferences prefAlarm;
Preferences prefWiFi;

// ============================================================
// WIFI / PORTAL
// ============================================================
WebServer server(80);
DNSServer dnsServer;

const char* AP_SSID = "ESP32-RADIO";

bool portalMode = false;

// ============================================================
// RADIO STATE
// ============================================================
bool radioPlaying = false;
bool radioStarting = false;

// ============================================================
// VOLUME
// ============================================================
uint8_t volumePercent = 50;

// ============================================================
// MAIN MODE
// ============================================================
enum MainMode {
  MODE_RADIO,
  MODE_CLOCK
};

MainMode mainMode = MODE_RADIO;
bool volumeMode = false;

// ============================================================
// BUTTON STATE
// ============================================================
struct ButtonState {
  bool stable;
  bool rawLast;
  unsigned long lastChange;
  unsigned long pressStart;
  bool longDone;
};

ButtonState btnRadio = {HIGH, HIGH, 0, 0, false};
ButtonState btnMode  = {HIGH, HIGH, 0, 0, false};

#define DEBOUNCE_MS      60
#define RADIO_HOLD_MS    800
#define MODE_HOLD_MS     2000
#define VOLUME_INTERVAL  300
#define VOLUME_TIMEOUT   3000

unsigned long lastVolumeActivity = 0;
unsigned long lastVolumeDown = 0;

// ============================================================
// NTP
// ============================================================
const long GMT_OFFSET_SEC = 7L * 3600L;
const int DAYLIGHT_OFFSET_SEC = 0;

bool ntpReady = false;

// ============================================================
// ALARM
// ============================================================
bool alarmEnabled = false;
int alarmHour = 6;
int alarmMinute = 30;
bool alarmTriggered = false;

// Alarm edit states
enum AlarmEdit {
  EDIT_HOUR,
  EDIT_MINUTE,
  EDIT_STATION
};

bool alarmSetup = false;
AlarmEdit alarmEdit = EDIT_HOUR;

// ============================================================
// DISPLAY TIMERS
// ============================================================
bool displayDirty = true;
MainMode displayedMode = MODE_RADIO;
bool displayedVolumeMode = false;
bool displayedAlarmSetup = false;
int displayedStation = -1;
int displayedVolume = -1;
bool displayedRadioPlaying = false;
bool displayedRadioStarting = false;
int lastClockSecond = -1;
unsigned long lastPortalDraw = 0;

// ============================================================
// WIFI STORAGE
// ============================================================
void saveWiFi(const String& ssid, const String& pass) {
  prefWiFi.begin("wifi", false);
  prefWiFi.putString("ssid", ssid);
  prefWiFi.putString("pass", pass);
  prefWiFi.end();
}

bool loadWiFi(String& ssid, String& pass) {
  prefWiFi.begin("wifi", true);
  ssid = prefWiFi.getString("ssid", "");
  pass = prefWiFi.getString("pass", "");
  prefWiFi.end();
  return ssid.length() > 0;
}

// ============================================================
// STATION STORAGE
// ============================================================
void saveStation() {
  prefRadio.begin("radio", false);
  prefRadio.putInt("station", currentStation);
  prefRadio.end();
}

void loadStation() {
  prefRadio.begin("radio", true);
  currentStation = prefRadio.getInt("station", 0);
  prefRadio.end();

  if (currentStation < 0 || currentStation >= stationCount)
    currentStation = 0;
}

// ============================================================
// VOLUME STORAGE
// ============================================================
void saveVolume() {
  prefRadio.begin("radio", false);
  prefRadio.putUChar("volume", volumePercent);
  prefRadio.end();
}

void loadVolume() {
  prefRadio.begin("radio", true);
  volumePercent = prefRadio.getUChar("volume", 20);
  prefRadio.end();

  if (volumePercent > 100)
    volumePercent = 20;
}

// ============================================================
// ALARM STORAGE
// ============================================================
void saveAlarm() {
  prefAlarm.begin("alarm", false);

  prefAlarm.putBool("enabled", alarmEnabled);
  prefAlarm.putInt("hour", alarmHour);
  prefAlarm.putInt("minute", alarmMinute);
  prefAlarm.putInt("station", alarmStation);

  prefAlarm.end();
}

void loadAlarm() {
  prefAlarm.begin("alarm", true);

  alarmEnabled = prefAlarm.getBool("enabled", false);
  alarmHour = prefAlarm.getInt("hour", 6);
  alarmMinute = prefAlarm.getInt("minute", 30);
  alarmStation = prefAlarm.getInt("station", 0);

  prefAlarm.end();

  if (alarmHour < 0 || alarmHour > 23)
    alarmHour = 6;

  if (alarmMinute < 0 || alarmMinute > 59)
    alarmMinute = 30;

  if (alarmStation < 0 || alarmStation >= stationCount)
    alarmStation = 0;
}

// ============================================================
// AUDIO
// ============================================================
void requestDisplay() {
  displayDirty = true;
}

void applyVolume() {
  if (out)
    out->SetGain((float)volumePercent / 100.0f);
}

void stopStream() {
  radioPlaying = false;
  radioStarting = false;
  bufferWarming = false;
  reconnectPending = false;

  if (decoder) {
    if (decoder->isRunning())
      decoder->stop();
    delete decoder;
    decoder = nullptr;
  }

  if (psramBuffer) {
    delete psramBuffer;
    psramBuffer = nullptr;
  }

  if (buffer) {
    delete buffer;
    buffer = nullptr;
  }

  if (stream) {
    delete stream;
    stream = nullptr;
  }

  // Beri allocator/stack waktu menyelesaikan cleanup sebelum alokasi stream baru.
  yield();
  requestDisplay();
}

void startRadio(bool isReconnect = false) {
  if (currentStation < 0 || currentStation >= stationCount) {
    Serial.println("[RADIO] currentStation tidak valid");
    radioPlaying = false;
    radioStarting = false;
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi belum tersambung");
    radioPlaying = false;
    radioStarting = false;
    requestDisplay();
    return;
  }

  if (!isReconnect) {
    reconnectAttempts = 0;
  }

  Serial.println();
  Serial.println("================================");
  Serial.print("START RADIO: ");
  Serial.println(stations[currentStation].name);
  Serial.print("URL: ");
  Serial.println(stations[currentStation].url);
  Serial.println("================================");

  // Batalkan reconnect lama sebelum membuka stream baru.
  reconnectPending = false;
  stopStream();
  delay(8);
  yield();

  Serial.print("[RADIO] HEAP after cleanup: ");
  Serial.println(ESP.getFreeHeap());

  radioStarting = true;
  radioPlaying = false;
  bufferWarming = false;
  requestDisplay();

  // I2S dibuat sekali di setup(), tidak diulang setiap ganti stasiun.
  if (!out) {
    out = new AudioOutputI2S();
    if (!out) {
      Serial.println("I2S allocation FAILED");
      radioStarting = false;
      requestDisplay();
      return;
    }
    out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  }

  // Mute selama buffer warm-up, seperti engine sketch 1.
  out->SetGain(0.0f);

  // HTTP tetap menggunakan jalur Rev9 yang sudah stabil.
  // HTTPS memakai secure source khusus ESP32-S3.
  if (stations[currentStation].url.startsWith("https://")) {
    Serial.println("[1] HTTPS secure stream + stable socket feeding");
    stream = new AudioFileSourceHTTPSStream(
      stations[currentStation].url.c_str()
    );
  } else {
    Serial.println("[1] HTTP/ICY stream");
    stream = new AudioFileSourceICYStream(
      stations[currentStation].url.c_str()
    );
  }

  if (!stream) {
    Serial.println("[ERROR] STREAM allocation FAILED");
    stopStream();
    return;
  }

  // Jangan teruskan ke decoder jika koneksi sebenarnya tidak terbuka.
  if (!stream->isOpen()) {
    Serial.println("[ERROR] STREAM TIDAK TERBUKA");
    stopStream();
    return;
  }

  Serial.println("[OK] stream terbuka");

  // HTTPS: gunakan PSRAM untuk buffer besar.
  // HTTP: tetap gunakan AudioFileSourceBuffer 256 KB seperti Rev11.
  bool isHTTPS = stations[currentStation].url.startsWith("https://");

  if (isHTTPS) {
    Serial.println("[2] HTTPS: PSRAM ring buffer 512 KB");

    if (ESP.getPsramSize() > 0) {
      psramBuffer = new PSRAMAudioFileSourceBuffer(
        stream,
        HTTPS_PSRAM_BUFFER_SIZE
      );

      if (!psramBuffer || !psramBuffer->isOpen()) {
        Serial.println("[HTTPS] PSRAM buffer gagal dibuat.");
        if (psramBuffer) {
          delete psramBuffer;
          psramBuffer = nullptr;
        }

        Serial.println("[HTTPS] Fallback ke buffer internal 256 KB");
        buffer = new AudioFileSourceBuffer(stream, AUDIO_BUFFER_SIZE);

        if (!buffer) {
          Serial.println("[ERROR] Fallback internal buffer allocation FAILED");
          stopStream();
          return;
        }
      }
    } else {
      Serial.println("[HTTPS] PSRAM tidak terdeteksi.");
      Serial.println("[HTTPS] Fallback ke buffer internal 256 KB");

      buffer = new AudioFileSourceBuffer(stream, AUDIO_BUFFER_SIZE);

      if (!buffer) {
        Serial.println("[ERROR] Internal buffer allocation FAILED");
        stopStream();
        return;
      }
    }
  } else {
    Serial.println("[2] HTTP/ICY: AudioFileSourceBuffer 256 KB");
    buffer = new AudioFileSourceBuffer(stream, AUDIO_BUFFER_SIZE);

    if (!buffer) {
      Serial.println("[ERROR] Audio buffer allocation FAILED");
      stopStream();
      return;
    }
  }

  const bool isAAC = (stations[currentStation].codec == "AAC");
  Serial.print("[3] ");
  Serial.println(isAAC ? "AAC decoder" : "MP3 decoder");

  decoder = isAAC ? static_cast<AudioGenerator*>(new AudioGeneratorAAC())
                  : static_cast<AudioGenerator*>(new AudioGeneratorMP3());
  if (!decoder) {
    Serial.println("[ERROR] decoder allocation FAILED");
    stopStream();
    return;
  }

  Serial.print("[4] ");
  Serial.print(isAAC ? "AAC" : "MP3");
  Serial.println(" begin");

  AudioFileSource* activeBuffer =
    psramBuffer ? static_cast<AudioFileSource*>(psramBuffer)
                : static_cast<AudioFileSource*>(buffer);

  if (!activeBuffer || !decoder->begin(activeBuffer, out)) {
    Serial.print("[ERROR] ");
    Serial.print(isAAC ? "AAC" : "MP3");
    Serial.println(" begin FAILED");
    stopStream();
    return;
  }

  radioPlaying = true;
  radioStarting = true;
  bufferWarming = true;
  radioStartTime = millis();
  reconnectPending = false;

  saveStation();

  Serial.println("[RADIO] CONNECTED");
  Serial.print("[RADIO] PROTOCOL: ");
  Serial.println(stations[currentStation].url.startsWith("https://") ? "HTTPS" : "HTTP");
  Serial.println("[RADIO] BUFFER WARM-UP 4000 ms (HTTP/HTTPS)");
  Serial.print("[RADIO] RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.print(" dBm  HEAP: ");
  Serial.println(ESP.getFreeHeap());

  requestDisplay();
}

void finishBufferWarmup() {
  if (!bufferWarming)
    return;

  if (millis() - radioStartTime < BUFFER_WARMUP_TIME)
    return;

  bufferWarming = false;
  radioStarting = false;
  reconnectAttempts = 0;

  if (out)
    out->SetGain((float)volumePercent / 100.0f);

  Serial.println("[RADIO] BUFFER READY");
  requestDisplay();
}

void handleAudio() {
  if (!decoder)
    return;

  if (decoder->isRunning()) {
    if (!decoder->loop()) {
      Serial.println("[AUDIO] Stream berhenti");
      if (decoder->isRunning())
        decoder->stop();
      radioPlaying = false;
      radioStarting = false;
      bufferWarming = false;
      if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        reconnectAttempts++;
        reconnectPending = true;
        reconnectTime = millis() + (RECONNECT_DELAY * reconnectAttempts);
        Serial.print("[AUDIO] Reconnect otomatis ");
        Serial.print(reconnectAttempts);
        Serial.print("/");
        Serial.println(MAX_RECONNECT_ATTEMPTS);
      } else {
        // Jangan reconnect tanpa batas. Station bermasalah harus berhenti
        // supaya Web Radio Manager dan tombol tetap dapat digunakan.
        reconnectPending = false;
        Serial.println("[AUDIO] Reconnect dihentikan setelah batas percobaan");
      }
      requestDisplay();
    }
  } else if (!reconnectPending) {
    Serial.println("[AUDIO] MP3 tidak running");
    radioPlaying = false;
    radioStarting = false;
    bufferWarming = false;
    if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
      reconnectAttempts++;
      reconnectPending = true;
      reconnectTime = millis() + (RECONNECT_DELAY * reconnectAttempts);
    } else {
      reconnectPending = false;
      Serial.println("[AUDIO] Reconnect dihentikan setelah batas percobaan");
    }
    requestDisplay();
  }
}

void handleReconnect() {
  if (!reconnectPending)
    return;

  if (millis() < reconnectTime)
    return;

  reconnectPending = false;

  if (WiFi.status() != WL_CONNECTED || mainMode != MODE_RADIO) {
    reconnectPending = false;
    return;
  }

  Serial.println("[RECONNECT] Restart radio");
  startRadio(true);
}

// ============================================================
// VOLUME
// ============================================================
void volumeUp() {
  if (volumePercent < 100)
    volumePercent += 5;

  if (volumePercent > 100)
    volumePercent = 100;

  applyVolume();
  saveVolume();

  Serial.print("VOLUME +5% = ");
  Serial.print(volumePercent);
  Serial.println("%");
}

void volumeDown() {
  if (volumePercent >= 5)
    volumePercent -= 5;
  else
    volumePercent = 0;

  applyVolume();
  saveVolume();

  Serial.print("VOLUME DOWN = ");
  Serial.print(volumePercent);
  Serial.println("%");
}

// ============================================================
// NTP
// ============================================================
void setupNTP() {
  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    "pool.ntp.org",
    "time.nist.gov",
    "time.google.com"
  );

  struct tm ti;

  if (getLocalTime(&ti, 10000)) {
    ntpReady = true;

    Serial.printf(
      "NTP READY %02d:%02d:%02d\n",
      ti.tm_hour,
      ti.tm_min,
      ti.tm_sec
    );
  } else {
    ntpReady = false;
    Serial.println("NTP belum siap");
  }
}

// ============================================================
// ALARM
// ============================================================
void checkAlarm() {
  if (!alarmEnabled || !ntpReady)
    return;

  struct tm ti;

  if (!getLocalTime(&ti, 100))
    return;

  int currentMinuteOfDay =
    ti.tm_hour * 60 + ti.tm_min;

  int alarmMinuteOfDay =
    alarmHour * 60 + alarmMinute;

  static int triggeredMinute = -1;

  if (currentMinuteOfDay != triggeredMinute)
    alarmTriggered = false;

  if (
    currentMinuteOfDay == alarmMinuteOfDay &&
    !alarmTriggered
  ) {
    alarmTriggered = true;
    triggeredMinute = currentMinuteOfDay;

    Serial.println();
    Serial.println("================================");
    Serial.println("          ALARM AKTIF");
    Serial.printf(
      "WAKTU NTP : %02d:%02d:%02d\n",
      ti.tm_hour,
      ti.tm_min,
      ti.tm_sec
    );
    Serial.print("STASIUN ALARM : ");
    Serial.println(stations[alarmStation].name);
    Serial.println("================================");

    currentStation = alarmStation;

    alarmSetup = false;
    volumeMode = false;
    mainMode = MODE_RADIO;
    setBacklight(true);

    saveStation();

    stopStream();
    delay(100);

    startRadio();

    Serial.println("ALARM -> MODE RADIO");
  }
}

// ============================================================
// ST7735 BACKLIGHT POWER MANAGEMENT
// ============================================================
void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

void backlightActivity() {
  // Setiap aktivitas tombol di MODE NTP menyalakan BL lagi 10 menit.
  if (mainMode == MODE_CLOCK) {
    setBacklight(true);
    backlightTimer = millis();
  }
}

void serviceBacklight() {
  if (mainMode == MODE_RADIO) {
    // MODE RADIO: BL selalu menyala.
    if (!backlightOn)
      setBacklight(true);
    return;
  }

  // MODE NTP/JAM: BL mati setelah 10 menit tanpa aktivitas.
  if (backlightOn && millis() - backlightTimer >= NTP_BL_TIMEOUT)
    setBacklight(false);
}

// ============================================================
// ST7735 DRAW HELPERS
// ============================================================
void drawHeader(const char* title) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 2);
  tft.print(title);
  tft.drawFastHLine(0, 12, 128, ST77XX_WHITE);
}
void drawRadioScreen() {
  drawHeader("ESP32-S3 INTERNET RADIO");

  tft.setTextColor(ST77XX_BLUE);
  tft.setCursor(2, 35);
  tft.setTextSize(2);
  tft.print("ESP32-S3");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(2, 55);
  tft.print("STATION ");
  tft.print(currentStation + 1);
  tft.print("/");
  tft.print(stationCount);

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(2, 65);
  tft.print(stations[currentStation].name);

  tft.setTextColor(ST77XX_RED);
  tft.setCursor(2, 75);
  tft.print("RSSI: ");

  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.RSSI());
    tft.print(" dBm");
  } else {
    tft.print("OFF");
  }

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(2, 85);
  tft.print("VOL : ");
  tft.print(volumePercent);
  tft.print("%");

  tft.setCursor(2, 100);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);

  if (volumeMode)
    tft.print("MODE VOLUME");
  else if (radioStarting)
    tft.print("BUFFERING");
  else if (radioPlaying)
    tft.print("PLAYING");
  else
    tft.print("STOP");

  // ==============================
  // IP ADDRESS WEB RADIO MANAGER
  // ==============================
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(2, 130);
  tft.print("WEB RADIO:");

  tft.setCursor(2, 140);

  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("WiFi OFF");
  }
}

void drawClockTimeOnly() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) {
    tft.fillRect(0, 25, 128, 20, ST77XX_BLACK);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(2, 30);
    tft.print("NTP...");
    return;
  }

  // Hanya area jam yang dihapus/digambar ulang.
  tft.fillRect(0, 36, 128, 24, ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(8, 40);

  if (ti.tm_hour < 10) tft.print("0");
  tft.print(ti.tm_hour);
  tft.print(":");
  if (ti.tm_min < 10) tft.print("0");
  tft.print(ti.tm_min);
  tft.print(":");
  if (ti.tm_sec < 10) tft.print("0");
  tft.print(ti.tm_sec);

  lastClockSecond = ti.tm_sec;
}

void drawClockScreen() {
  drawHeader("MODE JAM NTP");
  drawClockTimeOnly();

  struct tm ti;
  if (!getLocalTime(&ti, 10))
    return;

  const char* days[] = {
    "MINGGU", "SENIN", "SELASA", "RABU","KAMIS", "JUMAT", "SABTU"
  };
  const char* months[] = {
    "JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"
  };

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(2, 65);
  tft.print(days[ti.tm_wday]);
  tft.print(" ");
  if (ti.tm_mday < 10) tft.print("0");
  tft.print(ti.tm_mday);
  tft.print(" ");
  tft.print(months[ti.tm_mon]);
  tft.print(" ");
  tft.print(ti.tm_year + 1900);

  tft.setCursor(2, 75);
  tft.print("ALARM: ");
  tft.setTextColor(ST77XX_WHITE);
  if (alarmEnabled) {
    if (alarmHour < 10) tft.print("0");
    tft.print(alarmHour);
    tft.print(":");
    if (alarmMinute < 10) tft.print("0");
    tft.print(alarmMinute);
    tft.setCursor(2, 85);
    tft.print(" ");
    tft.print(stations[alarmStation].name);
  } else {
    tft.print("OFF");
  }

  tft.setCursor(2, 95);
  tft.print("WIB / NTP");
  tft.setCursor(2, 105);
  tft.print("HOLD MODE = SET ALARM");
}

void drawVolumeScreen() {
  drawHeader("VOLUME");
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(3);
  tft.setCursor(5, 45);
  tft.print(volumePercent);
  tft.print("%");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 80);
  tft.print("TAP  = +5%");
  tft.setCursor(5, 90);
  tft.print("HOLD = TURUN");
  tft.setCursor(5, 100);
  tft.print("DIAM 3 DETIK = RADIO");
}

void drawAlarmSetup() {
  drawHeader("SET ALARM");
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(2, 50);
  if (alarmHour < 10) tft.print("0");
  tft.print(alarmHour);
  tft.print(":");
  if (alarmMinute < 10) tft.print("0");
  tft.print(alarmMinute);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 75);
  tft.print("STATION:");
  tft.setCursor(2, 85);
  tft.print(stations[alarmStation].name);

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(2, 95);
  if (alarmEdit == EDIT_HOUR)
    tft.print("> JAM +1");
  else if (alarmEdit == EDIT_MINUTE)
    tft.print("> MENIT +1");
  else
    tft.print("> STATION +1");

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 105);
  tft.print("MODE = NEXT / SAVE");
}

void drawPortalScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 5);
  tft.println("WIFI SETUP");
  tft.setTextColor(ST77XX_BLUE);
  tft.setCursor(2, 25);
  tft.println("AP: ESP32-RADIO");
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 40);
  tft.println("IP: 192.168.4.1");
  tft.setCursor(2, 60);
  tft.println("Connect HP ke AP");
  tft.setCursor(2, 80);
  tft.println("Buka 192.168.4.1");
}

void updateDisplay() {
  unsigned long now = millis();

  if (portalMode) {
    if (displayDirty || now - lastPortalDraw >= 1000) {
      drawPortalScreen();
      lastPortalDraw = now;
      displayDirty = false;
    }
    return;
  }

  // Full redraw hanya jika tampilan memang berubah.
  bool fullRedraw = displayDirty ||
                    displayedMode != mainMode ||
                    displayedVolumeMode != volumeMode ||
                    displayedAlarmSetup != alarmSetup;

  if (fullRedraw) {
    if (alarmSetup)
      drawAlarmSetup();
    else if (volumeMode)
      drawVolumeScreen();
    else if (mainMode == MODE_CLOCK)
      drawClockScreen();
    else
      drawRadioScreen();

    displayedMode = mainMode;
    displayedVolumeMode = volumeMode;
    displayedAlarmSetup = alarmSetup;
    displayedStation = currentStation;
    displayedVolume = volumePercent;
    displayedRadioPlaying = radioPlaying;
    displayedRadioStarting = radioStarting;
    displayDirty = false;

    if (mainMode == MODE_CLOCK) {
      struct tm ti;
      if (getLocalTime(&ti, 10))
        lastClockSecond = ti.tm_sec;
    } else {
      lastClockSecond = -1;
    }
    return;
  }

  // Perubahan radio/volume/status tetap memicu satu redraw saja.
  if (mainMode == MODE_RADIO &&
      (displayedStation != currentStation ||
       displayedVolume != volumePercent ||
       displayedRadioPlaying != radioPlaying ||
       displayedRadioStarting != radioStarting)) {
    drawRadioScreen();
    displayedStation = currentStation;
    displayedVolume = volumePercent;
    displayedRadioPlaying = radioPlaying;
    displayedRadioStarting = radioStarting;
    return;
  }

  // MODE JAM: hanya refresh angka jam sekali per detik.
  if (mainMode == MODE_CLOCK && !alarmSetup && !volumeMode) {
    struct tm ti;
    if (getLocalTime(&ti, 0) && ti.tm_sec != lastClockSecond) {
      drawClockTimeOnly();
    }
  }
}

// ============================================================
// NEXT RADIO
// ============================================================
void nextRadio() {
  currentStation++;

  if (currentStation >= stationCount)
    currentStation = 0;

  startRadio();
}

// ============================================================
// RADIO BUTTON
// ============================================================
void processRadioButton() {
  unsigned long now = millis();
  bool raw = digitalRead(BUTTON_RADIO);

  if (raw != btnRadio.rawLast) {
    btnRadio.rawLast = raw;
    btnRadio.lastChange = now;
  }

  if (now - btnRadio.lastChange < DEBOUNCE_MS)
    return;

  if (raw == btnRadio.stable)
    return;

  btnRadio.stable = raw;

  // PRESS
  if (raw == LOW) {
    btnRadio.pressStart = now;
    btnRadio.longDone = false;
    return;
  }

  // RELEASE
  unsigned long duration =
    now - btnRadio.pressStart;

  // ALARM SETUP
  if (alarmSetup) {
    if (duration < RADIO_HOLD_MS) {
      if (alarmEdit == EDIT_HOUR) {
        alarmHour++;
        if (alarmHour > 23) alarmHour = 0;
      }
      else if (alarmEdit == EDIT_MINUTE) {
        alarmMinute++;
        if (alarmMinute > 59) alarmMinute = 0;
      }
      else {
        alarmStation++;
        if (alarmStation >= stationCount)
          alarmStation = 0;
      }
    }

    requestDisplay();
    return;
  }

  // VOLUME MODE
  if (volumeMode) {
    if (duration < RADIO_HOLD_MS)
      volumeUp();

    lastVolumeActivity = now;
    return;
  }

  // RADIO MODE
  if (mainMode == MODE_RADIO) {
    if (duration < RADIO_HOLD_MS)
      nextRadio();
  }
}

// ============================================================
// RADIO BUTTON HOLD SERVICE
// ============================================================
void serviceRadioHold() {
  if (btnRadio.stable != LOW)
    return;

  unsigned long now = millis();
  unsigned long held =
    now - btnRadio.pressStart;

  if (
    mainMode == MODE_RADIO &&
    !volumeMode &&
    !alarmSetup &&
    held >= RADIO_HOLD_MS &&
    !btnRadio.longDone
  ) {
    btnRadio.longDone = true;
    volumeMode = true;
    lastVolumeActivity = now;

    Serial.println("MODE VOLUME");
    requestDisplay();
  }

  if (
    volumeMode &&
    !alarmSetup &&
    held >= RADIO_HOLD_MS
  ) {
    if (!btnRadio.longDone) {
      btnRadio.longDone = true;
      lastVolumeDown = now;
      volumeDown();
    }

    if (now - lastVolumeDown >= VOLUME_INTERVAL) {
      lastVolumeDown = now;
      volumeDown();
    }

    lastVolumeActivity = now;
  }
}

// ============================================================
// MODE BUTTON
// ============================================================
void processModeButton() {
  unsigned long now = millis();
  bool raw = digitalRead(BUTTON_MODE);

  if (raw != btnMode.rawLast) {
    btnMode.rawLast = raw;
    btnMode.lastChange = now;
  }

  if (now - btnMode.lastChange < DEBOUNCE_MS)
    return;

  if (raw == btnMode.stable)
    return;

  btnMode.stable = raw;

  if (raw == LOW) {
    btnMode.pressStart = now;
    btnMode.longDone = false;

    // GPIO21 ditekan saat MODE NTP -> BL ON dan timer 10 menit diulang.
    if (mainMode == MODE_CLOCK)
      backlightActivity();

    return;
  }

  unsigned long duration =
    now - btnMode.pressStart;

  // ALARM SETUP
  if (alarmSetup) {
    if (duration < MODE_HOLD_MS) {
      if (alarmEdit == EDIT_HOUR) {
        alarmEdit = EDIT_MINUTE;
      }
      else if (alarmEdit == EDIT_MINUTE) {
        alarmEdit = EDIT_STATION;
      }
      else {
        alarmSetup = false;
        alarmEnabled = true;
        saveAlarm();

        Serial.println("ALARM SAVED");
      }
    }

    requestDisplay();
    return;
  }

  // NORMAL MODE
  if (duration < MODE_HOLD_MS) {
    volumeMode = false;

    if (mainMode == MODE_RADIO) {
      mainMode = MODE_CLOCK;
      backlightActivity();
      stopStream();
      Serial.println("MODE -> CLOCK");
    }
    else {
      mainMode = MODE_RADIO;
      setBacklight(true);
      Serial.println("MODE -> RADIO");
      startRadio();
    }

    requestDisplay();
  }
}

// ============================================================
// MODE BUTTON HOLD
// ============================================================
void serviceModeHold() {
  if (btnMode.stable != LOW)
    return;

  if (
    mainMode == MODE_CLOCK &&
    !alarmSetup &&
    !btnMode.longDone &&
    millis() - btnMode.pressStart >= MODE_HOLD_MS
  ) {
    btnMode.longDone = true;
    alarmSetup = true;
    alarmEdit = EDIT_HOUR;
    volumeMode = false;

    Serial.println("ALARM SETUP");

    requestDisplay();
  }
}

// ============================================================
// WIFI CONNECT
// ============================================================
bool connectWiFi(
  const String& ssid,
  const String& pass,
  unsigned long timeoutMs
) {
  Serial.println();
  Serial.print("Connecting WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // PERTAHANKAN - diperlukan pada pengujian Anda
  esp_wifi_set_max_tx_power(40);

  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < timeoutMs
  ) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI CONNECTED");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());

    saveWiFi(ssid, pass);

    return true;
  }

  Serial.println("WIFI FAILED");
  WiFi.disconnect(true);

  return false;
}

// ============================================================
// WEB RADIO MANAGER
// ============================================================
String htmlEscape(const String& in) {
  String outStr;
  outStr.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') outStr += "&amp;";
    else if (c == '<') outStr += "&lt;";
    else if (c == '>') outStr += "&gt;";
    else if (c == '"') outStr += "&quot;";
    else if (c == '\'') outStr += "&#39;";
    else outStr += c;
  }
  return outStr;
}

// ============================================================
// WEB CONTROL - VOLUME & MODE
// ============================================================
void handleWebVolume() {
  if (!server.hasArg("v")) {
    server.send(400, "text/plain", "Missing volume");
    return;
  }

  int v = constrain(server.arg("v").toInt(), 0, 100);
  volumePercent = (uint8_t)v;
  applyVolume();
  saveVolume();

  lastVolumeActivity = millis();
  requestDisplay();

  server.send(200, "text/plain", String(volumePercent));
}

void handleWebVolumeStep() {
  if (!server.hasArg("step")) {
    server.send(400, "text/plain", "Missing step");
    return;
  }

  int v = constrain((int)volumePercent + server.arg("step").toInt(), 0, 100);
  volumePercent = (uint8_t)v;
  applyVolume();
  saveVolume();

  lastVolumeActivity = millis();
  requestDisplay();

  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleWebMode() {
  if (!server.hasArg("set")) {
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
    return;
  }

  String mode = server.arg("set");
  mode.toLowerCase();

  volumeMode = false;
  alarmSetup = false;

  if (mode == "radio") {
    mainMode = MODE_RADIO;
    setBacklight(true);
    startRadio();
    Serial.println("WEB -> MODE RADIO");
  }
  else if (mode == "clock" || mode == "ntp" || mode == "jam") {
    mainMode = MODE_CLOCK;
    backlightActivity();
    stopStream();
    Serial.println("WEB -> MODE JAM NTP");
  }

  requestDisplay();

  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

String radioManagerHTML() {
  String html;
  html.reserve(24000);

  // ============================================================
  // RE11 - WEB RADIO MANAGER
  // Tampilan: 10 radio per halaman + tombol halaman + dropdown
  // Tidak mengubah fungsi audio/NTP/LittleFS/handler backend.
  // ============================================================
  const int perPage = 10;
  int totalPages = (stationCount + perPage - 1) / perPage;
  if (totalPages < 1) totalPages = 1;

  int page = 0;
  if (server.hasArg("page")) page = server.arg("page").toInt();
  if (page < 0) page = 0;
  if (page >= totalPages) page = totalPages - 1;

  int first = page * perPage;
  int last = first + perPage;
  if (last > stationCount) last = stationCount;

  html += F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<meta charset='utf-8'><title>ESP32 Radio Manager Rev11</title><style>");
  html += F("*{box-sizing:border-box}body{font-family:Arial,sans-serif;margin:0;padding:12px;background:#07152b;color:#fff}");
  html += F(".wrap{max-width:760px;margin:auto}.card{background:#0b2138;border:1px solid #24516b;border-radius:16px;padding:16px;margin-bottom:12px}");
  html += F("h2{margin:0 0 6px;color:#36e6ff;font-size:22px}.sub{color:#9db8c8;font-size:13px;margin-bottom:12px}");
  html += F(".topbar{display:flex;flex-wrap:wrap;gap:6px}.btn,button{display:inline-block;border:0;border-radius:9px;padding:10px 12px;margin:0;font-weight:bold;text-decoration:none;cursor:pointer;font-size:14px}");
  html += F(".green{background:#36ffc4;color:#00151e}.gray{background:#31516a;color:#fff}.danger{background:#ff6262;color:#fff}.blue{background:#36e6ff;color:#00151e}");
  html += F(".jump{display:flex;gap:7px;align-items:center;flex-wrap:wrap;margin-top:12px;padding-top:12px;border-top:1px solid #24516b}.jump label{font-size:13px;color:#9db8c8}");
  html += F("select{background:#06172a;color:#fff;border:1px solid #315d75;border-radius:9px;padding:10px;font-size:15px;min-width:210px}");
  html += F(".pages{display:flex;flex-wrap:wrap;gap:5px;margin-top:12px}.pagebtn{min-width:40px;text-align:center;background:#31516a;color:#fff;border-radius:8px;padding:9px 8px;text-decoration:none;font-weight:bold}.pagebtn.active{background:#36e6ff;color:#00151e}");
  html += F(".pageinfo{font-size:13px;color:#9db8c8;margin-top:9px}");
  html += F(".row{background:#102f4d;border:1px solid #1d5975;border-radius:12px;padding:12px;margin:8px 0}.playing{border-color:#36ffc4;box-shadow:0 0 0 1px #36ffc4}");
  html += F(".name{font-size:17px;font-weight:bold;color:#36e6ff}.num{display:inline-block;min-width:34px;color:#ffd34d;font-weight:bold}.url{font-size:12px;color:#a8bdc9;word-break:break-all;margin:5px 0 9px}");
  html += F(".status{font-size:11px;color:#36ffc4;font-weight:bold;margin-bottom:4px}.actions{display:flex;flex-wrap:wrap;gap:5px}.actions .btn{padding:9px 11px}.count{font-size:13px;color:#9db8c8}.playbtn.playingBtn{background:#36ffc4;color:#00151e}.playStatus{display:none}.row.playing .playStatus{display:block}");
  html += F(".controlgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:12px}");
  html += F(".controlbox{background:#102f4d;border:1px solid #1d5975;border-radius:12px;padding:12px}");
  html += F(".controltitle{font-size:13px;color:#9db8c8;margin-bottom:7px}.value{font-size:22px;font-weight:bold;color:#36ffc4;margin-bottom:7px}");
  html += F("input[type=range]{width:100%;accent-color:#36e6ff;cursor:pointer}.modebuttons{display:flex;gap:6px;flex-wrap:wrap}");
  html += F(".modebtn{flex:1;min-width:110px;text-align:center}.activeMode{background:#36ffc4;color:#00151e}");
  html += F("@media(max-width:480px){.controlgrid{grid-template-columns:1fr}}");
  html += F("@media(max-width:480px){body{padding:8px}.card{padding:12px}h2{font-size:19px}.btn,button{font-size:13px;padding:9px 10px}.pagebtn{min-width:36px;padding:8px 6px}.name{font-size:16px}.url{font-size:11px}}");
  html += F("</style></head><body><div class='wrap'>");

  html += F("<div class='card'><h2>ESP32-S3 INTERNET RADIO</h2>");
  html += F("<div class='sub'>Radio Manager Rev11 &mdash; 10 radio per halaman</div>");
  html += F("<div class='topbar'>");
  html += F("<a class='btn green' href='/radio/add'>+ TAMBAH RADIO</a>");
  html += F("<a class='btn gray' href='/'>REFRESH</a>");
  html += F("</div>");

  // ============================================================
  // PENGATUR VOLUME & PILIHAN MODE
  // ============================================================
  html += F("<div class='controlgrid'>");

  html += F("<div class='controlbox'>");
  html += F("<div class='controltitle'>VOLUME</div>");
  html += F("<div class='value'><span id='volValue'>");
  html += String(volumePercent);
  html += F("</span>%</div>");
  html += F("<input id='volSlider' type='range' min='0' max='100' step='5' value='");
  html += String(volumePercent);
  html += F("' oninput='volChanged(this.value)'>");
  html += F("<div class='actions' style='margin-top:8px'>");
  html += F("<button class='btn gray' onclick='volStep(-5)'>− 5</button>");
  html += F("<button class='btn green' onclick='volStep(5)'>+ 5</button>");
  html += F("<button class='btn danger' onclick='volSet(0)'>MUTE</button>");
  html += F("</div></div>");

  html += F("<div class='controlbox'>");
  html += F("<div class='controltitle'>PILIH MODE</div>");
  html += F("<div class='value'>");
  if (mainMode == MODE_RADIO) html += F("RADIO");
  else html += F("JAM NTP");
  html += F("</div>");
  html += F("<div class='modebuttons'>");
  html += F("<a class='btn modebtn ");
  if (mainMode == MODE_RADIO) html += F("activeMode");
  html += F("' href='/mode?set=radio'>📻 RADIO</a>");
  html += F("<a class='btn modebtn ");
  if (mainMode == MODE_CLOCK) html += F("activeMode");
  html += F("' href='/mode?set=clock'>🕐 JAM NTP</a>");
  html += F("</div></div></div>");

  html += F("<script>");
  html += F("function volSet(v){v=Math.max(0,Math.min(100,parseInt(v)||0));");
  html += F("document.getElementById('volSlider').value=v;document.getElementById('volValue').textContent=v;");
  html += F("fetch('/volume?v='+v).catch(()=>{});}");
  html += F("function volChanged(v){document.getElementById('volValue').textContent=v;");
  html += F("clearTimeout(window.vt);window.vt=setTimeout(()=>volSet(v),180);}");
  html += F("function volStep(s){let v=parseInt(document.getElementById('volSlider').value)+s;volSet(v);}");
  html += F("async function playRadio(btn){const idx=btn.dataset.index;if(!idx) return;btn.disabled=true;const old=btn.innerHTML;btn.innerHTML='⏳ PLAY...';try{const r=await fetch('/radio/play?i='+encodeURIComponent(idx)+'&ajax=1',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();document.querySelectorAll('.row').forEach(row=>{row.classList.remove('playing');const b=row.querySelector('.playbtn');const st=row.querySelector('.playStatus');if(b){b.classList.remove('playingBtn');b.disabled=false;b.innerHTML='▶ PLAY';}if(st)st.style.display='none';});const row=document.getElementById('radioRow-'+d.index);if(row){row.classList.add('playing');const b=row.querySelector('.playbtn');const st=row.querySelector('.playStatus');if(b){b.classList.add('playingBtn');b.disabled=false;b.innerHTML='● PLAYING';}if(st)st.style.display='block';row.scrollIntoView({behavior:'smooth',block:'nearest'});}}catch(e){btn.disabled=false;btn.innerHTML=old;alert('Gagal menjalankan radio: '+e.message);}}");
  html += F("</script>");

  // Dropdown untuk langsung menuju radio tertentu.
  html += F("<div class='jump'><label for='radioSelect'>Pilih radio:</label>");
  html += F("<select id='radioSelect' onchange=\"if(this.value!='')location.href='/?page='+Math.floor(parseInt(this.value)/10)\">");
  html += F("<option value=''>-- pilih nomor radio --</option>");
  for (int i = 0; i < stationCount; i++) {
    html += F("<option value='");
    html += String(i);
    html += F("'>");
    html += String(i + 1);
    html += F(" - ");
    html += htmlEscape(stations[i].name);
    html += F("</option>");
  }
  html += F("</select></div>");

  // Tombol halaman 1-10 (atau lebih bila stationCount ditambah).
  html += F("<div class='pages'>");
  for (int p = 0; p < totalPages; p++) {
    html += F("<a class='pagebtn");
    if (p == page) html += F(" active");
    html += F("' href='/?page=");
    html += String(p);
    html += F("'>");
    html += String(p + 1);
    html += F("</a>");
  }
  html += F("</div>");

  html += F("<div class='pageinfo'>");
  html += F("Radio ");
  if (stationCount > 0) {
    html += String(first + 1);
    html += F("&ndash;");
    html += String(last);
  } else {
    html += F("0");
  }
  html += F(" dari ");
  html += String(stationCount);
  html += F(" radio &nbsp;&bull;&nbsp; Halaman ");
  html += String(page + 1);
  html += F("/");
  html += String(totalPages);
  html += F("</div></div>");

  // Hanya 10 radio pada halaman aktif.
  html += F("<div class='card'>");
  for (int i = first; i < last; i++) {
    html += F("<div class='row");
    if (i == currentStation && radioPlaying) html += F(" playing");
    html += F("' id='radioRow-");
    html += String(i);
    html += F("'>");

    html += F("<div class='status playStatus'>● NOW PLAYING</div>");

    html += F("<div class='name'><span class='num'>");
    html += String(i + 1);
    html += F(".</span>");
    html += htmlEscape(stations[i].name);
    html += F("</div>");

    html += F("<div class='url'>");
    html += htmlEscape(stations[i].url);
    html += F("</div>");
    html += F("<div class='url'>FORMAT: <b>");
    html += (stations[i].codec == "AAC" ? "AAC" : "MP3");
    html += F("</b></div>");

    html += F("<div class='actions'>");
    html += F("<button type='button' class='btn green playbtn");
    if (i == currentStation && radioPlaying) html += F(" playingBtn");
    html += F("' data-index='");
    html += String(i);
    html += F("' onclick='playRadio(this)'>");
    if (i == currentStation && radioPlaying) html += F("● PLAYING");
    else html += F("▶ PLAY");
    html += F("</button>");

    html += F("<a class='btn gray' href='/radio/edit?i=");
    html += String(i);
    html += F("'>✎ EDIT</a>");

    html += F("<a class='btn danger' href='/radio/delete?i=");
    html += String(i);
    html += F("' onclick=\"return confirm('Hapus radio nomor ");
    html += String(i + 1);
    html += F("?')\">🗑 HAPUS</a>");

    html += F("<a class='btn gray' href='/radio/up?i=");
    html += String(i);
    html += F("'>↑</a>");

    html += F("<a class='btn gray' href='/radio/down?i=");
    html += String(i);
    html += F("'>↓</a>");
    html += F("</div></div>");
  }
  html += F("</div>");

  // Navigasi bawah juga disediakan agar tidak perlu kembali ke atas.
  if (totalPages > 1) {
    html += F("<div class='card'><div class='pages'>");
    if (page > 0) {
      html += F("<a class='pagebtn' href='/?page=");
      html += String(page - 1);
      html += F("'>&laquo; SEBELUMNYA</a>");
    }
    if (page < totalPages - 1) {
      html += F("<a class='pagebtn' href='/?page=");
      html += String(page + 1);
      html += F("'>BERIKUTNYA &raquo;</a>");
    }
    html += F("</div></div>");
  }

  html += F("</div></body></html>");
  return html;
}

void redirectRadioManager() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

bool getRadioIndex(int& idx) {
  if (!server.hasArg("i")) return false;
  idx = server.arg("i").toInt();
  return idx >= 0 && idx < stationCount;
}

void handleRadioRoot() {
  if (portalMode) {
    server.send(200, "text/html", portalHTML());
  } else {
    server.send(200, "text/html", radioManagerHTML());
  }
}

void handleRadioAddPage() {
  String html;
  html.reserve(5000);
  html += F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><meta charset='utf-8'><title>Tambah Radio</title>");
  html += F("<style>body{font-family:Arial;background:#07152b;color:#fff;padding:16px}.card{max-width:520px;margin:auto;background:#0b2138;padding:20px;border-radius:16px}h2{color:#36e6ff}input{width:100%;padding:13px;margin:7px 0 12px;background:#06172a;color:#fff;border:1px solid #315d75;border-radius:9px;font-size:16px}button,a{display:inline-block;padding:12px 16px;border:0;border-radius:9px;text-decoration:none;font-weight:bold}.save{background:#36e6ff;color:#00151e}.back{background:#31516a;color:#fff}</style></head><body><div class='card'>");
  html += F("<h2>Tambah Radio</h2><form method='POST' action='/radio/add'>");
  html += F("<label>Nama Radio</label><input name='name' required maxlength='80'>");
  html += F("<label>Alamat Stream</label><input name='url' required maxlength='240' placeholder='http:// atau https://'>");
  html += F("<label>Format Audio</label><select name='codec' style='width:100%;padding:13px;margin:7px 0 12px;background:#06172a;color:#fff;border:1px solid #315d75;border-radius:9px;font-size:16px'><option value='MP3' selected>MP3</option><option value='AAC'>AAC</option></select>");
  html += F("<button class='save' type='submit'>SIMPAN</button> <a class='back' href='/'>BATAL</a></form></div></body></html>");
  server.send(200, "text/html", html);
}

void handleRadioAdd() {
  String name  = server.arg("name");
  String url   = server.arg("url");
  String codec = server.arg("codec");
  name.trim(); url.trim(); codec.trim();
  codec.toUpperCase();
  if (codec != "AAC" && codec != "MP3") codec = "MP3";

  if (!name.length() || !url.length()) {
    server.send(400, "text/plain", "Nama dan URL wajib diisi");
    return;
  }

  if (stationCount >= MAX_STATIONS) {
    server.send(400, "text/plain", "Database penuh (maksimum 200 radio)");
    return;
  }

  stations[stationCount].name  = name;
  stations[stationCount].url   = url;
  stations[stationCount].codec = codec;
  stationCount++;
  saveRadioDatabase();

  redirectRadioManager();
}

void handleRadioEditPage() {
  int idx;
  if (!getRadioIndex(idx)) {
    server.send(400, "text/plain", "Index radio tidak valid");
    return;
  }

  String html;
  html.reserve(5000);
  html += F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><meta charset='utf-8'><title>Edit Radio</title>");
  html += F("<style>body{font-family:Arial;background:#07152b;color:#fff;padding:16px}.card{max-width:520px;margin:auto;background:#0b2138;padding:20px;border-radius:16px}h2{color:#36e6ff}input{width:100%;padding:13px;margin:7px 0 12px;background:#06172a;color:#fff;border:1px solid #315d75;border-radius:9px;font-size:16px}button,a{display:inline-block;padding:12px 16px;border:0;border-radius:9px;text-decoration:none;font-weight:bold}.save{background:#36e6ff;color:#00151e}.back{background:#31516a;color:#fff}</style></head><body><div class='card'>");
  html += F("<h2>Edit Radio #"); html += String(idx + 1); html += F("</h2><form method='POST' action='/radio/edit'>");
  html += F("<input type='hidden' name='i' value='"); html += String(idx); html += F("'>");
  html += F("<label>Nama Radio</label><input name='name' required maxlength='80' value='");
  html += htmlEscape(stations[idx].name); html += F("'>");
  html += F("<label>Alamat Stream</label><input name='url' required maxlength='240' value='");
  html += htmlEscape(stations[idx].url); html += F("'>");
  html += F("<label>Format Audio</label><select name='codec' style='width:100%;padding:13px;margin:7px 0 12px;background:#06172a;color:#fff;border:1px solid #315d75;border-radius:9px;font-size:16px'>");
  html += F("<option value='MP3'"); if (stations[idx].codec != "AAC") html += F(" selected"); html += F(">MP3</option>");
  html += F("<option value='AAC'"); if (stations[idx].codec == "AAC") html += F(" selected"); html += F(">AAC</option></select>");
  html += F("<button class='save' type='submit'>SIMPAN</button> <a class='back' href='/'>BATAL</a></form></div></body></html>");
  server.send(200, "text/html", html);
}

void handleRadioEdit() {
  int idx;
  if (!getRadioIndex(idx)) {
    server.send(400, "text/plain", "Index radio tidak valid");
    return;
  }

  String name  = server.arg("name");
  String url   = server.arg("url");
  String codec = server.arg("codec");
  name.trim(); url.trim(); codec.trim();
  codec.toUpperCase();
  if (codec != "AAC" && codec != "MP3") codec = "MP3";

  if (!name.length() || !url.length()) {
    server.send(400, "text/plain", "Nama dan URL wajib diisi");
    return;
  }

  stations[idx].name  = name;
  stations[idx].url   = url;
  stations[idx].codec = codec;
  saveRadioDatabase();

  if (idx == currentStation && mainMode == MODE_RADIO) {
    startRadio();
  }

  redirectRadioManager();
}

void handleRadioDelete() {
  int idx;
  if (!getRadioIndex(idx)) {
    server.send(400, "text/plain", "Index radio tidak valid");
    return;
  }

  bool deletingCurrent = (idx == currentStation);
  stopStream();

  for (int i = idx; i < stationCount - 1; i++)
    stations[i] = stations[i + 1];

  stationCount--;

  if (stationCount <= 0) {
    currentStation = -1;
    alarmStation = -1;
  } else {
    if (currentStation > idx) currentStation--;
    if (currentStation >= stationCount) currentStation = stationCount - 1;
    if (alarmStation > idx) alarmStation--;
    if (alarmStation >= stationCount) alarmStation = stationCount - 1;
  }

  saveStation();
  saveAlarm();
  saveRadioDatabase();

  if (deletingCurrent && mainMode == MODE_RADIO && stationCount > 0)
    startRadio();

  redirectRadioManager();
}

void handleRadioMoveUp() {
  int idx;
  if (!getRadioIndex(idx)) { server.send(400, "text/plain", "Index tidak valid"); return; }
  if (idx > 0) {
    RadioStation tmp = stations[idx];
    stations[idx] = stations[idx - 1];
    stations[idx - 1] = tmp;
    if (currentStation == idx) currentStation--;
    else if (currentStation == idx - 1) currentStation++;
    if (alarmStation == idx) alarmStation--;
    else if (alarmStation == idx - 1) alarmStation++;
    saveRadioDatabase(); saveStation(); saveAlarm();
  }
  redirectRadioManager();
}

void handleRadioMoveDown() {
  int idx;
  if (!getRadioIndex(idx)) { server.send(400, "text/plain", "Index tidak valid"); return; }
  if (idx < stationCount - 1) {
    RadioStation tmp = stations[idx];
    stations[idx] = stations[idx + 1];
    stations[idx + 1] = tmp;
    if (currentStation == idx) currentStation++;
    else if (currentStation == idx + 1) currentStation--;
    if (alarmStation == idx) alarmStation++;
    else if (alarmStation == idx + 1) alarmStation--;
    saveRadioDatabase(); saveStation(); saveAlarm();
  }
  redirectRadioManager();
}

void handleRadioPlay() {
  int idx;
  if (!getRadioIndex(idx)) { server.send(400, "text/plain", "Index tidak valid"); return; }
  // PLAY dari Web Manager selalu dianggap perintah baru.
  // Batalkan retry dari radio sebelumnya agar tidak mengambil alih.
  reconnectPending = false;
  reconnectAttempts = 0;
  currentStation = idx;
  saveStation();
  if (mainMode != MODE_RADIO) {
    mainMode = MODE_RADIO;
    volumeMode = false;
    alarmSetup = false;
    setBacklight(true);
  }
  startRadio();
  if (server.hasArg("ajax") && server.arg("ajax") == "1") {
    String json = "{\"index\":" + String(currentStation) + ",\"playing\":" + String(radioPlaying ? "true" : "false") + "}";
    server.send(200, "application/json", json);
    return;
  }
  redirectRadioManager();
}

bool webRoutesReady = false;

void setupRadioWebRoutes() {
  if (webRoutesReady) return;
  server.on("/", HTTP_GET, handleRadioRoot);

  server.on("/radio/add", HTTP_GET, handleRadioAddPage);
  server.on("/radio/add", HTTP_POST, handleRadioAdd);

  server.on("/radio/edit", HTTP_GET, handleRadioEditPage);
  server.on("/radio/edit", HTTP_POST, handleRadioEdit);

  server.on("/radio/delete", HTTP_GET, handleRadioDelete);
  server.on("/radio/up", HTTP_GET, handleRadioMoveUp);
  server.on("/radio/down", HTTP_GET, handleRadioMoveDown);
  server.on("/radio/play", HTTP_GET, handleRadioPlay);

  // WEB CONTROL
  server.on("/volume", HTTP_GET, handleWebVolume);
  server.on("/volume/step", HTTP_GET, handleWebVolumeStep);
  server.on("/mode", HTTP_GET, handleWebMode);

  server.on("/generate_204", HTTP_GET, handleRadioRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRadioRoot);
  server.on("/connecttest.txt", HTTP_GET, handleRadioRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRadioRoot);
  webRoutesReady = true;
}

// ============================================================
// CAPTIVE PORTAL
// ============================================================
String portalHTML() {
  String html;
  html.reserve(5000);

  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 RADIO</title>";

  html += "<style>";

  // ===== BODY =====
  html += "*{box-sizing:border-box}";
  html += "body{";
  html += "font-family:Arial,sans-serif;";
  html += "margin:0;";
  html += "padding:20px;";
  html += "background:linear-gradient(135deg,#07152b,#0b2a4a,#063b4c);";
  html += "color:#fff;";
  html += "min-height:100vh;";
  html += "}";

  // ===== CONTAINER =====
  html += ".container{";
  html += "max-width:480px;";
  html += "margin:auto;";
  html += "background:rgba(10,25,45,.94);";
  html += "padding:22px;";
  html += "border-radius:20px;";
  html += "box-shadow:0 10px 35px rgba(0,0,0,.45);";
  html += "border:1px solid rgba(0,220,255,.25);";
  html += "}";

  // ===== HEADER =====
  html += ".header{text-align:center;margin-bottom:22px}";
  html += ".header h2{";
  html += "margin:0;";
  html += "font-size:28px;";
  html += "color:#36e6ff;";
  html += "letter-spacing:2px;";
  html += "text-shadow:0 0 12px rgba(54,230,255,.5);";
  html += "}";
  html += ".header p{";
  html += "margin:8px 0 0;";
  html += "font-size:14px;";
  html += "color:#a9c8d8;";
  html += "}";

  // ===== SECTION TITLE =====
  html += ".section-title{";
  html += "font-size:16px;";
  html += "font-weight:bold;";
  html += "color:#36e6ff;";
  html += "margin:18px 0 10px;";
  html += "}";

  // ===== WIFI ITEM =====
  html += ".wifi-list{margin-bottom:15px}";

  html += ".wifi-item{";
  html += "display:block;";
  html += "background:linear-gradient(90deg,#102f4d,#123b5c);";
  html += "border:1px solid #1d5975;";
  html += "border-radius:12px;";
  html += "padding:12px;";
  html += "margin:8px 0;";
  html += "cursor:pointer;";
  html += "transition:.2s;";
  html += "}";

  html += ".wifi-item:hover{";
  html += "background:linear-gradient(90deg,#164768,#145b70);";
  html += "border-color:#36e6ff;";
  html += "transform:translateY(-1px);";
  html += "}";

  // ===== RADIO BUTTON =====
  html += ".wifi-item input[type=radio]{";
  html += "width:20px;";
  html += "height:20px;";
  html += "vertical-align:middle;";
  html += "margin-right:10px;";
  html += "accent-color:#00d9ff;";
  html += "}";

  // ===== SIGNAL =====
  html += ".signal{";
  html += "float:right;";
  html += "font-size:13px;";
  html += "color:#8eeaff;";
  html += "margin-top:3px;";
  html += "}";

  // ===== INPUT =====
  html += "input[type=text],input[type=password]{";
  html += "width:100%;";
  html += "padding:13px;";
  html += "margin:7px 0;";
  html += "font-size:17px;";
  html += "color:#fff;";
  html += "background:#081b30;";
  html += "border:1px solid #24516b;";
  html += "border-radius:10px;";
  html += "outline:none;";
  html += "}";

  html += "input[type=text]:focus,input[type=password]:focus{";
  html += "border-color:#36e6ff;";
  html += "box-shadow:0 0 8px rgba(54,230,255,.25);";
  html += "}";

  // ===== BUTTON =====
  html += "button{";
  html += "width:100%;";
  html += "padding:14px;";
  html += "margin-top:15px;";
  html += "font-size:18px;";
  html += "font-weight:bold;";
  html += "letter-spacing:1px;";
  html += "color:#00151e;";
  html += "background:linear-gradient(90deg,#00d9ff,#36ffc4);";
  html += "border:0;";
  html += "border-radius:12px;";
  html += "cursor:pointer;";
  html += "box-shadow:0 5px 18px rgba(0,220,255,.25);";
  html += "}";

  html += "button:hover{";
  html += "background:linear-gradient(90deg,#36ffc4,#00d9ff);";
  html += "transform:translateY(-1px);";
  html += "}";

  // ===== FOOTER =====
  html += ".footer{";
  html += "text-align:center;";
  html += "font-size:12px;";
  html += "color:#7295a8;";
  html += "margin-top:20px;";
  html += "}";

  html += "</style></head><body>";

  // ===== CONTAINER =====
  html += "<div class='container'>";

  html += "<div class='header'>";
  html += "<h2>ESP32 RADIO</h2>";
  html += "<p>WiFi Configuration</p>";
  html += "</div>";

  html += "<form action='/save' method='POST'>";

  html += "<div class='section-title'>AVAILABLE WiFi</div>";
  html += "<div class='wifi-list'>";

  int n = WiFi.scanNetworks();

  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;

    html += "<label class='wifi-item'>";

    html += "<input type='radio' name='ssid' value='";
    html += s;
    html += "'>";

    html += "<strong>";
    html += s;
    html += "</strong>";

    html += "<span class='signal'>";
    html += WiFi.RSSI(i);
    html += " dBm";
    html += "</span>";

    html += "</label>";
  }

  html += "</div>";

  html += "<div class='section-title'>MANUAL WiFi</div>";

  html += "<input type='text' name='manual_ssid' placeholder='SSID'>";

  html += "<input type='password' name='password' placeholder='Password'>";

  html += "<button type='submit'>SAVE & CONNECT</button>";

  html += "</form>";

  html += "<div class='footer'>";
  html += "ESP32 Internet Radio";
  html += "</div>";

  html += "</div>";

  html += "</body></html>";

  WiFi.scanDelete();

  return html;
}
void portalRoot() {
  server.send(200, "text/html", portalHTML());
}

void portalSave() {
  String ssid = server.arg("ssid");
  String manual = server.arg("manual_ssid");
  String pass = server.arg("password");

  if (manual.length())
    ssid = manual;

  if (!ssid.length()) {
    server.send(400, "text/plain", "SSID kosong");
    return;
  }

  saveWiFi(ssid, pass);

server.send(
  200,
  "text/html",
  "<!DOCTYPE html><html><head>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ESP32 RADIO</title>"
  "<style>"
  "body{"
    "margin:0;"
    "padding:20px;"
    "font-family:Arial,sans-serif;"
    "background:linear-gradient(135deg,#07152b,#0b2a4a,#063b4c);"
    "color:#fff;"
    "min-height:100vh;"
    "display:flex;"
    "align-items:center;"
    "justify-content:center;"
  "}"
  ".card{"
    "width:100%;"
    "max-width:420px;"
    "padding:30px 20px;"
    "text-align:center;"
    "background:rgba(10,25,45,.95);"
    "border-radius:20px;"
    "border:1px solid rgba(54,230,255,.3);"
    "box-shadow:0 10px 35px rgba(0,0,0,.5);"
  "}"
  ".icon{"
    "font-size:55px;"
    "margin-bottom:10px;"
  "}"
  "h2{"
    "margin:10px 0;"
    "color:#36e6ff;"
    "font-size:26px;"
    "text-shadow:0 0 12px rgba(54,230,255,.5);"
  "}"
  "p{"
    "color:#b5d3df;"
    "font-size:17px;"
    "margin-top:15px;"
  "}"
  ".loading{"
    "width:100%;"
    "height:7px;"
    "margin-top:25px;"
    "background:#102f4d;"
    "border-radius:10px;"
    "overflow:hidden;"
  "}"
  ".bar{"
    "height:100%;"
    "width:40%;"
    "background:linear-gradient(90deg,#00d9ff,#36ffc4);"
    "border-radius:10px;"
    "animation:load 1.5s infinite;"
  "}"
  "@keyframes load{"
    "0%{margin-left:-40%;}"
    "100%{margin-left:100%;}"
  "}"
  ".footer{"
    "margin-top:22px;"
    "font-size:12px;"
    "color:#7295a8;"
  "}"
  "</style></head><body>"
  "<div class='card'>"
  "<div class='icon'>📶</div>"
  "<h2>WiFi Disimpan</h2>"
  "<p>Mencoba terhubung...</p>"
  "<div class='loading'><div class='bar'></div></div>"
  "<div class='footer'>ESP32 INTERNET RADIO</div>"
  "</div>"
  "</body></html>"
);

  delay(1000);

  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);

  if (connectWiFi(ssid, pass, 15000)) {
    portalMode = false;
    setupNTP();
    startRadio();
  }
  else {
    startPortal();
  }
}

void portalNotFound() {
  server.sendHeader(
    "Location",
    "http://192.168.4.1/",
    true
  );

  server.send(302, "text/plain", "");
}

void startPortal() {
  portalMode = true;
  setBacklight(true);

  stopStream();

  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    AP_SSID,
    nullptr,
    1,
    false,
    4
  );

  delay(300);

  IPAddress ip = WiFi.softAPIP();

  Serial.println();
  Serial.println("================================");
  Serial.println("CAPTIVE PORTAL");
  Serial.print("AP: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(ip);
  Serial.println("================================");

  dnsServer.start(
    53,
    "*",
    ip
  );

  // Routes dibuat sekali; root otomatis menampilkan portal saat portalMode=true.
  setupRadioWebRoutes();
  static bool portalSaveRouteReady = false;
  if (!portalSaveRouteReady) {
    server.on("/save", HTTP_POST, portalSave);
    server.onNotFound(portalNotFound);
    portalSaveRouteReady = true;
  }

  server.begin();

  requestDisplay();
}

void servicePortal() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateDisplay();
}

// ============================================================
// INITIAL WIFI
// ============================================================
void connectInitialWiFi() {
  String ssid;
  String pass;

  if (loadWiFi(ssid, pass)) {
    Serial.println("Saved WiFi found");

    if (connectWiFi(ssid, pass, 12000)) {
      setupNTP();
      setupRadioWebRoutes();
      server.begin();
      return;
    }
  }

  if (
    connectWiFi(
      String(WIFI_SSID),
      String(WIFI_PASS),
      12000
    )
  ) {
    setupNTP();
    setupRadioWebRoutes();
    server.begin();
    return;
  }

  startPortal();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32-S3 SUPERMINI INTERNET RADIO");
  Serial.println("ST7735 + MAX98357A");
  Serial.println("HTTP + HTTPS MP3 / PSRAM BUFFER");
  Serial.println("================================");

  // PSRAM diagnostics.
  // Jika hasil total = 0, aktifkan PSRAM/QSPI PSRAM pada setting board
  // yang sesuai dengan varian SuperMini Anda.
  Serial.printf("[PSRAM] total : %u bytes\n", ESP.getPsramSize());
  Serial.printf("[PSRAM] free  : %u bytes\n", ESP.getFreePsram());

  if (ESP.getPsramSize() > 0) {
    Serial.println("[PSRAM] STATUS: AKTIF");
  } else {
    Serial.println("[PSRAM] STATUS: TIDAK TERDETEKSI");
    Serial.println("[PSRAM] HTTPS akan fallback ke buffer internal 256 KB.");
  }

  // BUTTONS
  pinMode(BUTTON_RADIO, INPUT_PULLUP);
  pinMode(BUTTON_MODE, INPUT_PULLUP);

  // TFT BACKLIGHT
  pinMode(TFT_BL, OUTPUT);
  setBacklight(true);
  backlightTimer = millis();

  btnRadio.stable = digitalRead(BUTTON_RADIO);
  btnRadio.rawLast = btnRadio.stable;
  btnRadio.lastChange = millis();

  btnMode.stable = digitalRead(BUTTON_MODE);
  btnMode.rawLast = btnMode.stable;
  btnMode.lastChange = millis();

  // TFT
  SPI.begin(
    TFT_SCLK,
    -1,
    TFT_MOSI,
    TFT_CS
  );

 tft.initR(INITR_BLACKTAB);
  tft.setTextWrap(false);

  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  tft.setCursor(2, 5);
  tft.println("ESP32-S3 RADIO");

  tft.setCursor(2, 20);
  tft.println("Starting...");

  // RADIO DATABASE
  initRadioDatabase();

  // LOAD NVS
  loadStation();
  loadVolume();
  loadAlarm();
  normalizeStationIndex();

  alarmTriggered = false;

  // WIFI
  connectInitialWiFi();

  if (portalMode) {
    requestDisplay();
    updateDisplay();
    return;
  }

  // I2S
  Serial.println("Creating I2S...");

  out = new AudioOutputI2S();

  if (!out) {
    Serial.println("I2S allocation FAILED");
    return;
  }

  out->SetPinout(
    I2S_BCLK,
    I2S_LRC,
    I2S_DOUT
  );

  applyVolume();

  Serial.println("I2S configured");

  // RADIO
  startRadio();

  requestDisplay();
  updateDisplay();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  if (portalMode) {
    servicePortal();
    delay(1);
    return;
  }

  // WEB RADIO MANAGER - wajib diproses saat mode STA/WiFi normal
  server.handleClient();

  // AUDIO - engine mengikuti sketch 1, non-blocking reconnect
  handleAudio();
  finishBufferWarmup();
  handleReconnect();

  // BUTTONS
  processRadioButton();
  serviceRadioHold();

  processModeButton();
  serviceModeHold();

  // BACKLIGHT POWER MANAGEMENT
  serviceBacklight();

  // VOLUME TIMEOUT
  if (
    volumeMode &&
    millis() - lastVolumeActivity >= VOLUME_TIMEOUT
  ) {
    volumeMode = false;

    Serial.println(
      "VOLUME TIMEOUT -> RADIO"
    );
  }

  // ALARM
  checkAlarm();

  // WIFI RECOVERY
  static unsigned long lastWiFiCheck = 0;

  if (millis() - lastWiFiCheck >= 5000) {
    lastWiFiCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected");

      stopStream();
      startPortal();
    }
  }

  // DISPLAY - renderer tetap dipanggil setiap loop, tetapi hanya menggambar
  // jika dirty / jam berubah; tidak ada full-screen refresh periodik.
  updateDisplay();

  delay(1);
}
