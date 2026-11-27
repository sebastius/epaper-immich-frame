#include "photos.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>

#include <deque>

#include "imaging.h"
#include "log.h"

namespace photos {

static constexpr int MAX_DOWNLOADS = 6;      // per ophaalbeurt, om een passende stand te vinden
static constexpr size_t RECENT = 30;        // zoveel recent getoonde foto's overslaan
static std::deque<String> recent;
static String albumId, albumFor;            // gevonden album-id en voor welke naam/URL

// ------------------------------------------------------------ HTTP

// Groeiende buffer in PSRAM als doel voor HTTPClient::writeToStream.
class Buffer : public Stream {
 public:
  uint8_t *data = nullptr;
  size_t len = 0, cap = 0;
  ~Buffer() { free(data); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *p, size_t n) override {
    if (len + n > cap) {
      size_t nc = cap ? cap : 64 * 1024;
      while (nc < len + n) nc *= 2;
      uint8_t *d = (uint8_t *)heap_caps_realloc(data, nc, MALLOC_CAP_SPIRAM);
      if (!d) return 0;
      data = d;
      cap = nc;
    }
    memcpy(data + len, p, n);
    len += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

struct Response {
  int code = 0;
  String contentType;
  Buffer body;
};

static bool request(const String &url, const char *method, const String &apiKey,
                    const String &jsonBody, Response &r) {
  bool https = url.startsWith("https://");
  NetworkClientSecure secure;
  NetworkClient plain;
  if (https) secure.useBuiltinCACertBundle();   // certificaat van de server echt controleren
  HTTPClient http;
  http.setTimeout(30000);
  http.setReuse(false);
  if (!http.begin(https ? (NetworkClient &)secure : plain, url)) {
    r.code = -1;
    return false;
  }
  const char *keys[] = {"Content-Type"};
  http.collectHeaders(keys, 1);
  if (apiKey.length()) http.addHeader("x-api-key", apiKey);
  if (jsonBody.length()) http.addHeader("Content-Type", "application/json");
  r.code = strcmp(method, "POST") == 0 ? http.POST(jsonBody) : http.GET();
  if (r.code > 0) {
    r.contentType = http.header("Content-Type");
    if (r.code == 200) http.writeToStream(&r.body);
  }
  http.end();
  return r.code == 200;
}

static String httpError(const Response &r) {
  if (r.code > 0) return "HTTP " + String(r.code);
  return HTTPClient::errorToString(r.code);
}

// ------------------------------------------------------------ Immich

static bool looksLikeId(const String &v) { return v.length() == 36 && v[8] == '-' && v[13] == '-'; }

static bool resolveAlbum(const Settings &s, String &err) {
  String key = s.immichUrl + "|" + s.immichAlbum;
  if (albumFor == key && albumId.length()) return true;
  albumId = "";
  if (looksLikeId(s.immichAlbum)) {
    albumId = s.immichAlbum;
    albumFor = key;
    return true;
  }
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["albumName"] = true;
  filter[0]["assetCount"] = true;
  for (const char *q : {"/api/albums", "/api/albums?shared=true"}) {
    Response r;
    if (!request(s.immichUrl + q, "GET", s.immichKey, "", r)) {
      err = "albums opvragen: " + httpError(r);
      return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, r.body.data, r.body.len, DeserializationOption::Filter(filter))) continue;
    for (JsonObject a : doc.as<JsonArray>()) {
      if (s.immichAlbum == (const char *)(a["albumName"] | "")) {
        albumId = a["id"].as<String>();
        albumFor = key;
        logf("album '%s' gevonden (%d foto's)\n", s.immichAlbum.c_str(), a["assetCount"] | 0);
        return true;
      }
    }
  }
  err = "album '" + s.immichAlbum + "' niet gevonden";
  return false;
}

struct Candidate {
  String id;
  int portrait = -1;   // -1 = onbekend, 0 = liggend, 1 = staand
};

// Uit exifInfo: staand of liggend (EXIF-oriëntatie 5..8 = gedraaid opgeslagen).
static int portraitFromExif(JsonObject e) {
  int w = e["exifImageWidth"] | 0, h = e["exifImageHeight"] | 0;
  if (!w || !h) return -1;
  int o = e["orientation"].is<const char *>() ? atoi(e["orientation"].as<const char *>())
                                              : (e["orientation"] | 1);
  if (o >= 5 && o <= 8) std::swap(w, h);
  return h > w;
}

static bool candidates(const Settings &s, std::vector<Candidate> &out, String &err) {
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["exifInfo"]["exifImageWidth"] = true;
  filter[0]["exifInfo"]["exifImageHeight"] = true;
  filter[0]["exifInfo"]["orientation"] = true;

  Response r;
  String body = "{\"albumIds\":[\"" + albumId + "\"],\"type\":\"IMAGE\",\"size\":40,\"withExif\":true}";
  JsonDocument doc;
  if (request(s.immichUrl + "/api/search/random", "POST", s.immichKey, body, r) &&
      !deserializeJson(doc, r.body.data, r.body.len, DeserializationOption::Filter(filter))) {
    for (JsonObject a : doc.as<JsonArray>())
      out.push_back({a["id"].as<String>(), portraitFromExif(a["exifInfo"])});
    return true;
  }
  // Oudere Immich-versies: alle foto's van het album ophalen.
  logf("search/random gaf %s, val terug op het album\n", httpError(r).c_str());
  JsonDocument f2;
  f2["assets"][0]["id"] = true;
  f2["assets"][0]["type"] = true;
  f2["assets"][0]["exifInfo"] = filter[0]["exifInfo"];
  Response r2;
  if (!request(s.immichUrl + "/api/albums/" + albumId, "GET", s.immichKey, "", r2)) {
    err = "album opvragen: " + httpError(r2);
    return false;
  }
  JsonDocument d2;
  if (deserializeJson(d2, r2.body.data, r2.body.len, DeserializationOption::Filter(f2))) {
    err = "album: onleesbaar antwoord";
    return false;
  }
  for (JsonObject a : d2["assets"].as<JsonArray>())
    if (a["type"] == "IMAGE") out.push_back({a["id"].as<String>(), portraitFromExif(a["exifInfo"])});
  // willekeurige volgorde
  for (size_t i = out.size(); i > 1; i--) std::swap(out[i - 1], out[esp_random() % i]);
  return true;
}

static bool isRecent(const String &id) {
  for (auto &r : recent)
    if (r == id) return true;
  return false;
}

static Result fromImmich(const Settings &s, uint8_t *frame) {
  Result res;
  res.sleepSeconds = 300;
  String err;
  if (!resolveAlbum(s, err)) {
    res.message = err;
    return res;
  }
  std::vector<Candidate> list;
  if (!candidates(s, list, err)) {
    res.message = err;
    return res;
  }
  if (list.empty()) {
    res.message = "album is leeg";
    return res;
  }

  const bool wantAny = s.orientation == "auto";
  const int want = s.orientation == "portrait";
  // Volgorde: nieuw en passend, dan nieuw en onbekend, dan recent; niet-passende achteraan.
  auto rank = [&](const Candidate &c) {
    int r = isRecent(c.id) ? 2 : 0;
    if (!wantAny && c.portrait >= 0 && c.portrait != want) r += 4;
    else if (!wantAny && c.portrait < 0) r += 1;
    return r;
  };
  std::stable_sort(list.begin(), list.end(),
                   [&](const Candidate &a, const Candidate &b) { return rank(a) < rank(b); });

  int downloads = 0;
  for (auto &c : list) {
    if (downloads >= MAX_DOWNLOADS) break;
    Response r;
    downloads++;
    if (!request(s.immichUrl + "/api/assets/" + c.id + "/thumbnail?size=preview", "GET",
                 s.immichKey, "", r)) {
      res.message = "foto downloaden: " + httpError(r);
      continue;
    }
    if (r.contentType.indexOf("webp") >= 0) {
      res.message = "Immich levert WebP-previews; zet bij Image Settings het preview-formaat op JPEG";
      return res;
    }
    int w, h;
    if (!img::jpegSize(r.body.data, r.body.len, w, h)) {
      res.message = "foto " + c.id + " is geen bruikbare JPEG (" + r.contentType + ")";
      continue;
    }
    bool portrait = h > w;
    if (!wantAny && (int)portrait != want && downloads < MAX_DOWNLOADS) continue;

    bool rotate = s.orientation == "portrait" || (wantAny && portrait);
    int rotation = rotate ? s.rotation : 0;
    float cx = 0.5f, cy = 0.35f;   // bovenkant van de foto iets meer in beeld
    if (rotation == 90) { cx = 0.35f; cy = 0.5f; }
    else if (rotation == 270) { cx = 0.65f; cy = 0.5f; }

    uint32_t t = millis();
    img::Image im;
    if (!img::decodeJpeg(r.body.data, r.body.len, rotation, im)) {
      res.message = "decoderen mislukt";
      continue;
    }
    uint32_t td = millis();
    img::Params p{s.saturation, s.contrast, s.brightness, s.warmth};
    bool ok = img::render(im, cx, cy, p, frame);
    logf("foto %dx%d -> %dx%d: decoderen %lu ms, omzetten %lu ms\n", w, h, im.w, im.h,
         (unsigned long)(td - t), (unsigned long)(millis() - td));
    img::freeImage(im);
    if (!ok) {
      res.message = "omzetten mislukt (geheugen?)";
      continue;
    }
    recent.push_back(c.id);
    if (recent.size() > RECENT) recent.pop_front();
    res.ok = true;
    res.sleepSeconds = s.refreshSeconds;
    res.message = "foto " + c.id + (portrait ? " (staand" : " (liggend") + (rotate ? ", gedraaid)" : ")");
    return res;
  }
  if (res.message.isEmpty()) res.message = "geen geschikte foto gevonden";
  return res;
}

// ------------------------------------------------------------ publiek

Result fetchFrame(const Settings &s, uint8_t *frame) {
  if (s.immichUrl.isEmpty()) return {false, 300, "geen IMMICH_URL in config.txt"};
  if (s.immichKey.isEmpty()) return {false, 300, "geen IMMICH_API_KEY in config.txt"};
  return fromImmich(s, frame);
}

}  // namespace photos
