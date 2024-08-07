#include "hardware/pio.h"
#include <pico.h>
#include "SPI.h"
#include "SdFat.h"
#include "Adafruit_SPIFlash.h"
#include "Adafruit_TinyUSB.h"
#include "BufferedPrint.h"
// for flashTransport definition
#include "flash_config.h"

#define PIN_SCL 3u
#define PIN_SDA 2u
#define PIN_SWITCH 4u
#define TIMEOUT 1000
PIO pio = pio0;
uint sm;
char pcommandType;
uint32_t paddress;
uint32_t pdata;
uint offset;
volatile bool isfastmode = false;
volatile void (*core1fun)() = nullptr;
typedef FatFile file_t;
// file system object from SdFat
FatVolume fatfs;
file_t file;
BufferedPrint<file_t, 64> bp;
Adafruit_SPIFlash flash(&flashTransport);
Adafruit_USBD_MSC usb_msc;

static const uint16_t pgm[32] = {
  /*     .wrap_target*/
  0x80a0,  /*  0: pull   block*/
  0xe027,  /*  1: set    x, 7*/
  0x7001,  /*  2: out    pins, 1         side 0*/
  0x1842,  /*  3: jmp    x--, 2          side 1*/
  0x00c9,  /*  4: jmp    pin, 9*/
  0xe02f,  /*  5: set    x, 15*/
  0x7001,  /*  6: out    pins, 1         side 0*/
  0x1846,  /*  7: jmp    x--, 6          side 1*/
  0x0000,  /*  8: jmp    0*/
  0xf880,  /*  9: set    pindirs, 0      side 1*/
  0xf02e,  /* 10: set    x, 14           side 0*/
  0x5801,  /* 11: in     pins, 1         side 1*/
  0x104b,  /* 12: jmp    x--, 11         side 0*/
  0x5801,  /* 13: in     pins, 1         side 1*/
  0x8020,  // 14: push   block
  0xe081,  /* 15: set    pindirs, 1*/
  0x0000,  /* 16: jmp    0*/
           /*     .wrap*/
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
  0x0000,
};
static const struct pio_program piopgm = {
  .instructions = pgm,
  .length = 32,
  .origin = -1
};
void inline pwrite(uint8_t addr, uint16_t data) {
  //Serial.println(data,HEX);
  uint32_t rdata;
  rdata = (addr << 17) | (0x0 << 16) | data;
  pio_sm_put_blocking(pio, sm, rdata << 8);
}
uint16_t inline pread(uint8_t addr) {
  uint32_t rdata;
  rdata = (addr << 1) | 0x1;
  pio_sm_put_blocking(pio, sm, rdata << 24);
  return static_cast<uint16_t>(pio_sm_get_blocking(pio, sm) & 0xFFFF);
}
void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  flash.begin();
  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("Adafruit", "External Flash", "1.0");
  // Set callback
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  // Set disk size, block size should be 512 regardless of spi flash page size
  usb_msc.setCapacity(flash.size() / 512, 512);
  // MSC is ready for read/write
  usb_msc.setUnitReady(true);
  usb_msc.begin();
  // Init file system on the flash
  if (!fatfs.begin(&flash)) {
    Serial.println("Please format your pico first");
  }
  gpio_put(PIN_SWITCH, false);
  pio_init();
}
void loop() {

  if (BOOTSEL) {
    file_t file2;
    while (BOOTSEL) { tight_loop_contents(); }
    file2.open("/commands.txt");
    if (!file2) {
      Serial.println("Cannot open file");
      file2.close();
      return;
    }
    char line[100];
    digitalWrite(LED_BUILTIN, HIGH);
    while (file2.available()) {
      int n = file2.fgets(line, sizeof(line));
      if (n <= 0) {
        Serial.println("fgets failed");
        break;
      }
      if (line[n - 1] != '\n' && n == (sizeof(line) - 1)) {
        Serial.println("line too long,or you are using a Mac?LOL");
        break;
      }
      String str = line;
      str.trim();
      praseline(str);
    }
    digitalWrite(LED_BUILTIN, LOW);
    file2.close();
  }
  if (Serial.available() > 0) {
    String commandSTR = Serial.readStringUntil('\n');
    commandSTR.trim();
    praseline(commandSTR);
    Serial.println("Done");
  }
}
void waitack(void) {
  while (pread(0x0c) != 0x00) { Serial.println("ack"); }
}
void rst(void) {
  gpio_put(PIN_SDA, false);
  delayMicroseconds(1);
  gpio_put(PIN_SDA, true);
  //return (pread(0x0) == 0x480);
}
void beginset(void) {
  gpio_put(PIN_SCL, true);
  gpio_put(PIN_SDA, false);
  delayMicroseconds(10);
  gpio_put(PIN_SDA, true);
  if (pread(0x00) != 0x480) {
    pwrite(0x00, 0xAAFE);
    if (pread(0x00) != 0x480) {
      pwrite(0x0E, 0x0000);
      pwrite(0x0D, 0x0008);
      if (pread(0x00) == 0x480 && pread(0x0E) == 0x0004) {
        pwrite(0x0D, 0x0000);
        pwrite(0x0E, 0x0000);
        pwrite(0x0D, 0x0000);
      }
    }
    /*read serial*/
    Serial.println("modle:");
    const char saddr[] = {
      0x41,
      0x40,
      0x43,
      0x42,
      0x51,
      0x50,
      0x53,
      0x52,
    };
    for (int i = 0; i < sizeof(saddr); i++) {
      Serial.print(pread(saddr[i++]), HEX);
      Serial.println(pread(saddr[i]), HEX);
    }
    Serial.print("Lock:");
    Serial.println(pread(0x48), HEX);
  }
  Serial.println("Trying classic password");
  pwrite(0x4a, 0x3137);
  pwrite(0x4b, 0x4143);
  pwrite(0x4c, 0x4753);
  pwrite(0x4d, 0x5961);
  pwrite(0x4e, 0x6771);
  pwrite(0x4f, 0x7379);
  pwrite(0x44, 0x2307);
  pwrite(0x46, 0x1719);
  pwrite(0x47, 0x
