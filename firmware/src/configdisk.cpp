#include "configdisk.h"

#include <LittleFS.h>
#include <USBMSC.h>
#include <esp_heap_caps.h>

#include "diskio_impl.h"
#include "ff.h"
#include "log.h"
#include "settings.h"

namespace configdisk {

static constexpr uint32_t SECTOR = 512;
static constexpr uint32_t SECTORS = 1024;              // 512 KB
static constexpr size_t SIZE = SECTOR * SECTORS;
static constexpr const char *IMAGE_PATH = "/disk.img";
static constexpr uint32_t SETTLE_MS = 2000;

static uint8_t *disk = nullptr;     // wat de computer ziet
static uint8_t *snap = nullptr;     // kopie om zelf (met FatFs) te lezen
static uint8_t *fatImg = nullptr;   // waar FatFs nu op werkt
static BYTE pdrv = 0xFF;
static char drive[8];
static USBMSC msc;
static volatile bool dirty = false;
static volatile uint32_t lastWrite = 0;
static String current;
// FATFS en FIL bevatten elk een sectorbuffer van FF_MAX_SS (4 KB): niet op de stack.
static FATFS fs;
static FIL file;

// ------------------------------------------------------------ FatFs op een RAM-beeld

static DSTATUS ramInit(BYTE) { return 0; }
static DSTATUS ramStatus(BYTE) { return 0; }
static DRESULT ramRead(BYTE, BYTE *buf, uint32_t sector, unsigned count) {
  if (sector + count > SECTORS) return RES_PARERR;
  memcpy(buf, fatImg + sector * SECTOR, count * SECTOR);
  return RES_OK;
}
static DRESULT ramWrite(BYTE, const BYTE *buf, uint32_t sector, unsigned count) {
  if (sector + count > SECTORS) return RES_PARERR;
  memcpy(fatImg + sector * SECTOR, buf, count * SECTOR);
  return RES_OK;
}
static DRESULT ramIoctl(BYTE, BYTE cmd, void *buf) {
  switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buf = SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buf = SECTOR; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buf = 1; return RES_OK;
  }
  return RES_PARERR;
}
static const ff_diskio_impl_t ramImpl = {ramInit, ramStatus, ramRead, ramWrite, ramIoctl};

static String path(const char *name) { return String(drive) + name; }

static bool readConfig(uint8_t *img, String &out) {
  fatImg = img;
  if (f_mount(&fs, drive, 1) != FR_OK) return false;
  FIL &f = file;
  bool ok = f_open(&f, path("/config.txt").c_str(), FA_READ) == FR_OK;
  if (ok) {
    out = "";
    char buf[257];
    UINT n;
    while (f_read(&f, buf, 256, &n) == FR_OK && n > 0) {
      buf[n] = 0;
      out += buf;
    }
    f_close(&f);
  }
  f_mount(nullptr, drive, 0);
  return ok;
}

static bool format(uint8_t *img) {
  fatImg = img;
  memset(img, 0, SIZE);
  MKFS_PARM opt = {FM_FAT | FM_SFD, 1, 0, 0, 0};
  uint8_t *work = (uint8_t *)malloc(4096);
  FRESULT r = f_mkfs(drive, &opt, work, 4096);
  free(work);
  if (r != FR_OK) {
    logf("ERR schijfje formatteren: %d\n", r);
    return false;
  }
  if (f_mount(&fs, drive, 1) != FR_OK) return false;
  f_setlabel(path("FOTOLIJST").c_str());
  FIL &f = file;
  if (f_open(&f, path("/config.txt").c_str(), FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
    UINT n;
    const char *txt = defaultConfigText();
    f_write(&f, txt, strlen(txt), &n);
    f_close(&f);
  }
  f_mount(nullptr, drive, 0);
  logf("nieuw config-schijfje aangemaakt\n");
  return true;
}

static void save(const uint8_t *img) {
  File f = LittleFS.open(IMAGE_PATH, "w");
  if (!f) {
    logf("ERR kan schijfje niet opslaan\n");
    return;
  }
  f.write(img, SIZE);
  f.close();
}

// ------------------------------------------------------------ USB-massaopslag

static int32_t onRead(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  size_t pos = (size_t)lba * SECTOR + offset;
  if (pos + size > SIZE) return -1;
  memcpy(buf, disk + pos, size);
  return size;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  size_t pos = (size_t)lba * SECTOR + offset;
  if (pos + size > SIZE) return -1;
  memcpy(disk + pos, buf, size);
  lastWrite = millis();
  dirty = true;
  return size;
}

static bool onStartStop(uint8_t, bool start, bool eject) {
  if (eject && !start) logf("schijfje uitgeworpen\n");
  return true;
}

// ------------------------------------------------------------ publiek

String begin() {
  disk = (uint8_t *)heap_caps_malloc(SIZE, MALLOC_CAP_SPIRAM);
  snap = (uint8_t *)heap_caps_malloc(SIZE, MALLOC_CAP_SPIRAM);
  if (!disk || !snap) {
    logf("ERR geen PSRAM voor het config-schijfje\n");
    return "";
  }
  if (ff_diskio_get_drive(&pdrv) != ESP_OK) {
    logf("ERR geen FatFs-schijfnummer vrij\n");
    return "";
  }
  ff_diskio_register(pdrv, &ramImpl);
  snprintf(drive, sizeof drive, "%u:", pdrv);

  if (!LittleFS.begin(true)) logf("ERR LittleFS niet beschikbaar; instellingen blijven niet bewaard\n");

  bool loaded = false;
  File f = LittleFS.open(IMAGE_PATH, "r");
  if (f && f.size() == SIZE) loaded = f.read(disk, SIZE) == SIZE;
  if (f) f.close();

  if (!loaded || !readConfig(disk, current)) {
    format(disk);
    save(disk);
    readConfig(disk, current);
  }
  return current;
}

void startUsb() {
  msc.vendorID("Fotolijs");      // max. 8 tekens
  msc.productID("Config");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  msc.begin(SECTORS, SECTOR);
}

bool poll(String &configText) {
  if (!dirty || millis() - lastWrite < SETTLE_MS) return false;
  dirty = false;
  memcpy(snap, disk, SIZE);
  save(snap);
  String txt;
  if (!readConfig(snap, txt)) {
    logf("config.txt niet gevonden op het schijfje\n");
    return false;
  }
  if (txt == current) return false;
  current = txt;
  configText = txt;
  logf("config.txt gewijzigd\n");
  return true;
}

}  // namespace configdisk
