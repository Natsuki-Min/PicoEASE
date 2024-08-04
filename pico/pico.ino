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
bool iswaitforvpp=true;
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
  pwrite(0x47, 0x2329);
  pwrite(0x45, 0x1113);
  Serial.print("Lock:");
  Serial.println(pread(0x48), HEX);
  if (pread(0x48) != 0x0) {
    Serial.println("Trying default password");
    delay(100);
    pwrite(0x4a, 0xFFFF);
    pwrite(0x4b, 0xFFFF);
    pwrite(0x4c, 0xFFFF);
    pwrite(0x4d, 0xFFFF);
    pwrite(0x4e, 0xFFFF);
    pwrite(0x4f, 0xFFFF);
    pwrite(0x44, 0xFFFF);
    pwrite(0x46, 0xFFFF);
    pwrite(0x47, 0xFFFF);
    pwrite(0x45, 0xFFFF);
    Serial.print("Lock:");
    Serial.println(pread(0x48), HEX);
  }
}
void pio_init(void) {
  if (pio_can_add_program(pio, &piopgm)) {
    sm = pio_claim_unused_sm(pio, false);
    if (sm == -1) {
      while (1) {
        Serial.println("fail to claim sm");
      }
    }
    offset = pio_add_program(pio, &piopgm);
    i2c_program_init(pio, sm, offset, PIN_SDA, PIN_SCL);
  } else {
    while (1) {
      Serial.println("fail to add pgm");
    }
  }
  pio_sm_put_blocking(pio, sm, 0x1 << 24);
  for(int i=0;i<TIMEOUT;i++){
    delay(1);
    if(!pio_sm_is_rx_fifo_empty(pio, sm)){
      pio_sm_get(pio, sm);
      return;
    }
  }
  while(1){
    Serial.println("Your IO Is Broken,change it");
  }
}
static inline void i2c_program_init(PIO pio, uint sm, uint offset, uint pin_sda, uint pin_scl) {
  assert(pin_scl == pin_sda + 1);
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_out_pins(&c, pin_sda, 1);
  sm_config_set_set_pins(&c, pin_sda, 1);
  sm_config_set_in_pins(&c, pin_sda);
  sm_config_set_sideset_pins(&c, pin_scl);
  sm_config_set_jmp_pin(&c, pin_sda);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
  sm_config_set_out_shift(&c, false, false, 24);
  sm_config_set_in_shift(&c, false, false, 16);
  sm_config_set_sideset(&c, 2, true, false);
  sm_config_set_wrap(&c, offset, offset + piopgm.length - 1);
  float div = (float)30;
  sm_config_set_clkdiv(&c, div);
  pio_gpio_init(pio, pin_sda);
  /*gpio_set_oeover(pin_sda, GPIO_OVERRIDE_INVERT);*/
  pio_gpio_init(pio, pin_scl);
  /*gpio_set_oeover(pin_scl, GPIO_OVERRIDE_INVERT);*/
  gpio_pull_up(pin_scl);
  gpio_pull_down(pin_sda);
  gpio_put(pin_sda, true);
  gpio_put(pin_scl, true);
  uint32_t both_pins = (1u << pin_sda) | (1u << pin_scl);
  pio_sm_set_pins_with_mask(pio, sm, both_pins, both_pins);
  pio_sm_set_pindirs_with_mask(pio, sm, both_pins, both_pins);
  pio_sm_set_pins_with_mask(pio, sm, 0, both_pins);
  /* Configure and start SM*/
  pio_sm_init(pio, sm, offset, &c);
  pio_sm_set_enabled(pio, sm, true);
}
void waitforvpp(uint8_t unlock)｛
Serial.println("Plug vpp,then press any key to continue");
 unsigned long currentMillis = millis();
  while (!Serial.available() && !BOOTSEL && !iswaitforvpp) {
    if (currentMillis - previousMillis >= 500) {
      previousMillis = currentMillis;
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
    currentMillis = millis();
  }
  while (Serial.available()) {
    Serial.read(); 
  }
digitalWrite(LED_BUILTIN, unlock==0x1?HIGH:LOW);
rst();
pwrite(0x67,unlock);
}
void parseString(const String &str) {
  char type;
  int hex[2] = { 0 };
  bool readingType = true;
  int readingHex = 0;
  for (char c : str) {
    if (c == ' ') {
      if (readingType) {
        readingType = false;
        readingHex = 0;
      } else if (readingHex < 2) {
        readingHex = readingHex + 1;
      } else {
        break;
      }
    } else {
      if (readingType) {
        type = c;
      } else {
        if (c >= '0' && c <= '9') {
          hex[readingHex] = hex[readingHex] * 16 + (c - '0');
        } else if (c >= 'A' && c <= 'F') {
          hex[readingHex] = hex[readingHex] * 16 + (c - 'A' + 10);
        }
      }
    }
  }
  pcommandType = type;
  pdata = hex[1];
  paddress = hex[0];
}
/*-------cpu registers--------
0 r/w[0:3] cpu_status unclear(read(debug:480 run:4A0/4B0 rst:4A2)write(bit0:reset_mode bit1:(when write to bit0 turns to 1(reg62.8 also turns to 1),turns to 0 after R 0 and then excite bit0) bit2/bit3:unclear)
1 unclear
2 ?/w IR_debug_H instruction_register(only available in NMICE,not real IR)
3 ?/w IR_debug_L
4 r ER0 
5 r EA
6 r IB_chip instruction_bus
7 r LR
8 r ELR1
9 r ELR2
a r ELR3
b r [LCSR,ECSR1,ECSR2,ECSR3] may put the cart before the horse
------cpu control---------
c r/w[?] ICON instruction_control(read(bit0:(1 when commands in IR_debug is running))write(write 1 to start running commands in IR_debug.can also run when the chip is locked,but cant read ER0 when locked.When commands in IR_debug is not intact,it will excute the instructions circularly and set r0 to 0x10.When begin to run commands,PC will be set to 0x10))
d r/w[?] NMICECON ice_interrupt_control unclear(write(When chip is running,write 0x8 to trig ICE_interrupt.When in NMICE mode,write 0x20 then excute dw_FE7F or RTI to resume operation))
e r/w[?] NMICEFLG ice_interrupt_flag unclear(read(reason of the interrupt))
f
10
11
12
13
14
15
16
17
18
19
1a
1b
1c
1d
1e
1f
------password------
44-47,4a-4f
------serial-------
40-43,50-53
------flash------
60 r/w[?] FLASHCON0 unclear(maybe mode 1:mostly used in erasing,seldom in reading(one word read). 3:mostly used in batch of reading(may increase address automaticly) 5:mostly used in writing)
61 r/w[?] FLASHCON1 unclear(write 1 to read,4 to write,5 to erase,6 to chipersse)
62 r/w[0:7] FLADHSTA unclear(read([8:11]:(0 idle ,1 busy),[12:15]:(unclear,often 0 in write,1 in ersse),[0:7]:(often 1f))write(unclear))
63 ?/w FLASHA0 for segment
64 ?/w FLASHA1 for address
65 ?/w FLASHD0 for data input
66 r FLASHD1 for data output
67 r/w[?] FLASHACP (bit0 is used to enable erasing and programming the flash memory.0: Erasing and programming the flash memory is disabled (Initial value)1: Erasing and programming the flash memory is enabled)
*/
void CSRRead(uint32_t addroffset, size_t dataSize) {
  uint16_t addr = addroffset & 0xFFFF, segment = addroffset >> 16;
  rst();
  pwrite(0x60, 0x3);
  Serial.print(":02000002");
  Serial.print(intToHexString(segment, 1));
  Serial.print("000");
  Serial.println(intToHexString((uint8_t)(~(0x04 + ((uint8_t)(segment & 0xF) << 4)) + 1), 2));
  while (dataSize > (size_t)(0x10000 - addr)) {
    pwrite(0x64, addr);
    pwrite(0x63, segment);
    bin2hex(addroffset, 0x10000 - addr);
    dataSize -= (size_t)(0x10000 - addr);
    addroffset += (size_t)(0x10000 - addr);
    segment = addroffset >> 16;
    addr = addroffset & 0xFFFF;
    Serial.print(":02000002");
    Serial.print(intToHexString(segment, 1));
    Serial.print("000");
    Serial.println(intToHexString((uint8_t)(~(0x04 + ((uint8_t)(segment & 0xF) << 4)) + 1), 2));
  }
  pwrite(0x64, addr);
  pwrite(0x63, segment);
  bin2hex(addroffset, dataSize);
  Serial.println(":00000001FF");
}
void bin2hex(uint32_t addroffset, size_t dataSize) {
  uint8_t record[16], i, size;
  uint16_t addr = addroffset & 0xFFFF, segment = addroffset >> 16;
  int sum;
  for (size_t index = 0; index < dataSize; index += 16) {
    memset(record, 0, sizeof(record));
    i = 0;
    sum = 0;
    size_t chunkSize = min(16, dataSize - index);
    for (size_t j = 0; j < chunkSize; j = j + 2) {
      pwrite(0x61, 0x1);
      uint16_t rd = pread(0x66);
      record[j] = rd & 0xFF;
      record[j + 1] = rd >> 8;
      sum += (record[j] + record[j + 1]);
    }
    sum = sum + chunkSize + ((uint8_t)(addr >> 8)) + ((uint8_t)addr);
    Serial.print(':');
    Serial.print(intToHexString(chunkSize, 2));
    Serial.print(intToHexString(addr, 4));
    Serial.print("00");
    for (size = 0; size < chunkSize; ++size) {
      Serial.print(intToHexString(record[size], 2));
    }
    Serial.print(intToHexString((unsigned char)(-sum), 2));
    Serial.println();
    addr += 0x10;
  }
}
String intToHexString(int num, int length) {
  String hexString = "";
  /* Convert integer to hexadecimal string*/
  do {
    /* Get the least significant nibble (4 bits)*/
    int nibble = num & 0xF;
    /* Convert nibble to hexadecimal character*/
    char hexChar = (nibble < 10) ? ('0' + nibble) : ('A' + (nibble - 10));
    /* Prepend the character to the hexString*/
    hexString = hexChar + hexString;
    /* Shift number right by 4 bits (one hexadecimal digit)*/
    num >>= 4;
  } while (num > 0);
  /* Pad with zeros if necessary*/
  while (hexString.length() < length) {
    hexString = "0" + hexString;
  }
  return hexString;
}
void flasherase(uint32_t block) {
  waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  pwrite(0x60, 0x1); /*enter erase mode*/
  pwrite(0x63, block >> 16);
  pwrite(0x64, block & 0xFFFF); /*write addr*/
  pwrite(0x61, 0x5);            /*begin*/
  int j;
  j = 0;
  while ((pread(0x61) != 0x5) && (j < TIMEOUT)) { j++; }
  uint16_t value;
  delay(50);
  rst();
  while (((value = pread(0x62)) != 0x101F && value != 0x1F) && (j < TIMEOUT)) {
    j++;
    delayMicroseconds(10);
  }
  if (j >= TIMEOUT) {
    Serial.print("0X62:0x");
    Serial.println(pread(0x62), HEX);
    Serial.println("fail");
  }
  pwrite(0x60, 0x0);
  pwrite(0x61, 0x0);
  gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  
}
void flasheraseall() {
  waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  uint32_t block = 0;
  int j;
  for (int i = 0; i < 16; i++) {
    block = i * 0x4000;
    pwrite(0x60, 0x1); /*enter erase mode*/
    pwrite(0x63, block >> 16);
    pwrite(0x64, block & 0xFFFF); /*write addr*/
    pwrite(0x61, 0x5);            /*begin*/
    j = 0;
    while ((pread(0x61) != 0x5) && (j < TIMEOUT)) { j++; }
    delay(50);
    uint16_t value;
    while (((value = pread(0x62)) != 0x101F && value != 0x1F) && (j < TIMEOUT)) {
      j++;
      delayMicroseconds(10);
    }
    if (j >= TIMEOUT) {
      Serial.print("0X62:0x");
      Serial.println(pread(0x62), HEX);
      Serial.println("fail");
    }
    pwrite(0x60, 0x0);
    pwrite(0x61, 0x0);
    
  }
  gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  if (j >= TIMEOUT) {
    Serial.println("fail");
  }
  flashfill(0xfc00, 0x200, 0xffff);
}
void flashwrite(uint32_t offset, uint32_t dataSize, const uint8_t *data) {
  dataSize /= 2;
  waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  pwrite(0x60, 0x5); /*enter write mode*/
  pwrite(0x63, offset >> 16);
  pwrite(0x64, offset & 0xFFFF); /*write addr*/
  int j;
  for (int i = 0; i < dataSize; i++) {
    rst();
    pwrite(0x65, (data[i * 2]) | (data[i * 2 + 1] << 8));
    pwrite(0x61, 0x4); /*begin*/
    j = 0;
    while (pread(0x61) != 0x4) {
      if (j > TIMEOUT) {
        gpio_put(PIN_SWITCH, false);
        pwrite(0x67, 0x0);
        Serial.println("fail");
        Serial.print("0X61:0x");
        Serial.println(pread(0x61), HEX);
        Serial.println(i, HEX);
        return;
      }
      j++;
    }
    uint16_t value;
    while ((value = pread(0x62)) != 0x1F && value != 0x101F) {
      if (j > TIMEOUT) {
        gpio_put(PIN_SWITCH, false);
        pwrite(0x67, 0x0);
        Serial.println("fail");
        Serial.print("0X62:0x");
        Serial.println(pread(0x62), HEX);
        Serial.println(i, HEX);
        return;
      }
      j++;
      delayMicroseconds(10);
    }
    
  }
  gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  
}
void flashwritefromFlash(uint32_t offset, const char *filePath) {
  file.open(filePath);
  if (!file) {
    Serial.println(filePath);
    Serial.println("Cannot open file");
    return;
  }
waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  pwrite(0x60, 0x5); /*enter write mode*/
  pwrite(0x63, offset >> 16);
  pwrite(0x64, offset & 0xFFFF); /*write addr*/
  int lastseg = offset >> 16, j;
  while (file.available()) {
    rst();
    uint8_t data[2];
    data[0] = file.read();
    data[1] = file.read();
    pwrite(0x65, (data[0]) | (data[1] << 8));
    pwrite(0x61, 0x4); /*begin*/
    j = 0;
    while (pread(0x61) != 0x4) {
      if (j > TIMEOUT) {
        gpio_put(PIN_SWITCH, false);
        pwrite(0x67, 0x0);
        Serial.println("fail");
        Serial.print("0X61:0x");
        Serial.println(pread(0x61), HEX);
        Serial.println(offset, HEX);
        return;
      }
      j++;
    }
    uint16_t value;
    while ((value = pread(0x62)) != 0x1F && value != 0x101F) {
      if (j > TIMEOUT) {
        gpio_put(PIN_SWITCH, false);
        pwrite(0x67, 0x0);
        Serial.println("fail");
        Serial.print("0X62:0x");
        Serial.println(pread(0x62), HEX);
        Serial.println(offset, HEX);
        return;
      }
      j++;
      delayMicroseconds(10);
    }
    
    offset += 2;
    if ((offset >> 16) != lastseg) {
      pwrite(0x63, offset >> 16);
      pwrite(0x64, offset & 0xFFFF); /*write addr*/
    }
  }
  gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  file.close();
}
void flashfill(uint32_t offset, uint32_t dataSize, uint16_t data) {
  dataSize /= 2;
  waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  pwrite(0x60, 0x5); /*enter write mode*/
  pwrite(0x63, offset >> 16);
  pwrite(0x64, offset & 0xFFFF); /*write addr*/
  int j;
  for (int i = 0; i < dataSize; i++) {
    pwrite(0x65, data);
    pwrite(0x61, 0x4); /*begin*/
    j = 0;
    while (pread(0x61) != 0x4) {
      if (j > TIMEOUT) {
        gpio_put(PIN_SWITCH, false);
        pwrite(0x67, 0x0);
        Serial.println("fail");
        Serial.print("0X61:0x");
        Serial.println(pread(0x61), HEX);
        Serial.println(i, HEX);
        return;
      }
      j++;
    }
    uint16_t value;
    while ((value = pread(0x62)) != 0x1F && value != 0x101F) {
      if (j > TIMEOUT) {
        gpio_put(PIN_SWITCH, false);
        pwrite(0x67, 0x0);
        Serial.println("fail");
        Serial.print("0X62:0x");
        Serial.println(pread(0x62), HEX);
        Serial.println(i, HEX);
        return;
      }
      j++;
      delayMicroseconds(10);
    }
    
  }
  gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }

}
void InitializeFlash() {
  waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  pwrite(0x60, 0x1); /*enter erase mode*/
  pwrite(0x63, 0x0);
  pwrite(0x64, 0x0); /*write addr*/
  pwrite(0x61, 0x6); /*begin*/
  int j;
  j = 0;
  while ((pread(0x61) != 0x6) && (j < TIMEOUT)) { j++; }
  uint16_t value;
  delay(50);
  while (((value = pread(0x62)) != 0x101F && value != 0x1F) && (j < TIMEOUT)) {
    j++;
    delayMicroseconds(10);
  }
  if (j >= TIMEOUT) {
    Serial.print("0X62:0x");
    Serial.println(pread(0x62), HEX);
    Serial.println("fail");
  }
  pwrite(0x60, 0x0);
  pwrite(0x61, 0x0);
  
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  flashfill(0xfc00, 0x200, 0xffff);
}
uint32_t RunCommand(uint32_t command) {
  pwrite(0x2, command >> 16);
  pwrite(0x3, command & 0xFFFF);
  pwrite(0xc, 0x1);
  while (pread(0xc) != 0x0) { tight_loop_contents(); }
  return (pread(0x4) << 16) | (pread(0x5));
}
void flashwritemode(uint32_t offset) {
  uint32_t dataSize = 0;
  waitforvpp(0x1);
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  gpio_put(PIN_SWITCH, true);
  pwrite(0x60, 0x5); /*enter write mode*/
  pwrite(0x63, offset >> 16);
  pwrite(0x64, offset & 0xFFFF); /*write addr*/
  int j;
  bool end = false;
  uint8_t hex[1024] = { 0 };
  while (!end) {
    if (Serial.available() > 0) {
      String commandSTR = Serial.readStringUntil('\n');
      for (char c : commandSTR) {
        if (c >= '0' && c <= '9') {
          hex[dataSize / 8] = hex[dataSize / 8] * 16 + (c - '0');
          dataSize += 4;
        } else if (c >= 'A' && c <= 'F') {
          hex[dataSize / 8] = hex[dataSize / 8] * 16 + (c - 'A' + 10);
          dataSize += 4;
        } else if (c == 'Q') {
          end = true;
        }
      }
      dataSize /= 16;
      for (int i = 0; i < dataSize; i++) {
        pwrite(0x65, (hex[i * 2]) | (hex[i * 2 + 1] << 8));
        pwrite(0x61, 0x4); /*begin*/
        j = 0;
        while (pread(0x61) != 0x4) {
          if (j > TIMEOUT) {
            gpio_put(PIN_SWITCH, false);
            pwrite(0x67, 0x0);
            Serial.println("fail");
            Serial.print("0X61:0x");
            Serial.println(pread(0x61), HEX);
            Serial.println(i, HEX);
            return;
          }
          j++;
        }
        uint16_t value;
        while ((value = pread(0x62)) != 0x1F && value != 0x101F) {
          if (j > TIMEOUT) {
            gpio_put(PIN_SWITCH, false);
            pwrite(0x67, 0x0);
            Serial.println("fail");
            Serial.print("0X62:0x");
            Serial.println(pread(0x62), HEX);
            Serial.println(i, HEX);
            return;
          }
          j++;
          delayMicroseconds(10);
        }
      
      }
      Serial.println("OK");
    }
  }gpio_put(PIN_SWITCH, false);
  waitforvpp(0x0);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
}
#define QUEUE_LENGTH 65536

struct MyQueue {
  uint16_t data[QUEUE_LENGTH];
  int head;
  int tail;
};

volatile MyQueue queue = { {}, 0, 0 };

void enqueue(volatile struct MyQueue *q, uint16_t value) {
  int nextTail = (q->tail + 1) % QUEUE_LENGTH;
  if (nextTail != q->head) {
    q->data[q->tail] = value;
    q->tail = nextTail;
  } else {
    Serial.println("Queue full, data lost");
  }
}

bool dequeue(volatile struct MyQueue *q, uint16_t *value) {
  if (q->head != q->tail) {
    *value = q->data[q->head];
    q->head = (q->head + 1) % QUEUE_LENGTH;
    return true;
  } else {
    return false;
  }
}
void loop1() {
  while (isfastmode) {
    core1fun();
  }
}

void inline binreadfast(uint32_t addroffset, size_t dataSize) {
  for (size_t j = 0; j < dataSize; j = j + 2) {
    while ((pio->fstat & (1u << (PIO_FSTAT_TXFULL_LSB + sm))) != 0) { tight_loop_contents(); }
    pio->txf[sm] = (((0x61 << 17) | (0x0 << 16) | 0x1) << 8);
    while ((pio->fstat & (1u << (PIO_FSTAT_TXFULL_LSB + sm))) != 0) { tight_loop_contents(); }
    pio->txf[sm] = ((0x66 << 1) | 0x1) << 24;
    if ((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + sm))) == 0) {
      enqueue(&queue, pio->rxf[sm]);
    } else {
      if (queue.head != queue.tail) {
        uint32_t data_to_send = queue.data[queue.head];
        if (rp2040.fifo.push_nb(data_to_send)) {
          queue.head = (queue.head + 1) % QUEUE_LENGTH;
        } else {
          continue;
        }
      } else {
        continue;
      }
    }
  }

  while (((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + sm))) == 0)) { enqueue(&queue, pio->rxf[sm]); }
  while (pio_sm_get_tx_fifo_level(pio, sm) != 0) { tight_loop_contents(); }
  while (((pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + sm))) == 0)) { enqueue(&queue, pio->rxf[sm]); }
  while (queue.head != queue.tail) {
    uint32_t data_to_send = queue.data[queue.head];
    if (rp2040.fifo.push_nb(data_to_send)) {
      queue.head = (queue.head + 1) % QUEUE_LENGTH;
    } else {
      continue;
    }
  }
}
void fastROMRead(uint32_t addroffset, size_t dataSize) {
  core1fun = &fastROMReadCore1;
  isfastmode = true;
  uint16_t addr = addroffset & 0xFFFF, segment = addroffset >> 16;
  rst();
  pwrite(0x60, 0x3);
  while (dataSize > (size_t)(0x10000 - addr)) {
    pwrite(0x64, addr);
    pwrite(0x63, segment);
    binreadfast(addroffset, 0x10000 - addr);
    dataSize -= (size_t)(0x10000 - addr);
    addroffset += (size_t)(0x10000 - addr);
    segment = addroffset >> 16;
    addr = addroffset & 0xFFFF;
  }
  pwrite(0x64, addr);
  pwrite(0x63, segment);
  binreadfast(addroffset, dataSize);
}
volatile void fastROMReadCore1() {
  uint16_t rd = rp2040.fifo.pop();
  Serial.write(rd & 0xFF);
  Serial.write(rd >> 8);
}
void ROMSave(uint32_t addroffset, size_t dataSize, const char *filePath) {
  if (!file.open(filePath, O_WRITE | O_CREAT)) {
    Serial.print(filePath);
    Serial.println("File open failed.");
    return;
  }
  bp.begin(&file);
  uint16_t addr = addroffset & 0xFFFF, segment = addroffset >> 16;
  rst();
  pwrite(0x60, 0x3);
  while (dataSize > (size_t)(0x10000 - addr)) {
    pwrite(0x64, addr);
    pwrite(0x63, segment);
    for (size_t j = 0; j < 0x10000 - addr; j = j + 2) {
      pwrite(0x61, 0x1);
      uint16_t rd = pread(0x66);
      bp.print((char)(rd & 0xFF));
      bp.print((char)(rd >> 8));
    }
    dataSize -= (size_t)(0x10000 - addr);
    addroffset += (size_t)(0x10000 - addr);
    segment = addroffset >> 16;
    addr = addroffset & 0xFFFF;
  }
  pwrite(0x64, addr);
  pwrite(0x63, segment);
  for (size_t j = 0; j < dataSize; j = j + 2) {
    pwrite(0x61, 0x1);
    uint16_t rd = pread(0x66);
    bp.print((char)(rd & 0xFF));
    bp.print((char)(rd >> 8));
  }
  bp.sync();
  file.close();
}

void praseline(String commandSTR) {
  if (commandSTR.length() > 0) {
    parseString(commandSTR);
    switch (pcommandType) {
      case 'W':
        pwrite(paddress, pdata);
        break;
      case 'R':
        {
          uint16_t result = pread(paddress);
          Serial.print("0x");
          Serial.println(result, HEX);
          break;
        }
      case 'T':
        pio->sm[0].clkdiv = paddress;
        break;
      case 'A':
        CSRRead(paddress, pdata);
        break;
      case 'E':
        flasherase(paddress);
        break;
      case 'B':
        beginset();
        break;
      case 'F':
        flashfill(paddress, pdata, 0xFFFF);
        break;
      case 'X':
        {
          uint32_t result = RunCommand(paddress);
          Serial.print("R0:0x");
          Serial.println(result >> 16, HEX);
          Serial.print("EA:0x");
          Serial.println(result & 0xFFFF, HEX);
          break;
        }
      case 'C':
        {
          uint8_t arr[2];
          arr[0] = pdata & 0xFF;
          arr[1] = pdata >> 8;
          flashwrite(paddress, 2, arr);
          break;
        }
      case 'D':
        flasheraseall();
        break;
      case 'Q':
        flashwritemode(paddress);
        break;
      case 'S':
        InitializeFlash();
        break;
      case 'G':
        fastROMRead(paddress, pdata);
        break;
      case 'I':
        {
          commandSTR.trim();  // Removes leading and trailing whitespaces, including '\n'
          int spaceIndex = commandSTR.lastIndexOf(' ');
          String extractedString = commandSTR.substring(spaceIndex + 1);
          //Serial.println(extractedString); //I don't know why,but when you use -o3 must use it
          flashwritefromFlash(paddress, extractedString.c_str());
          break;
        }
      case 'H':
        {
          commandSTR.trim();
          int spaceIndex = commandSTR.lastIndexOf(' ');
          String extractedString = commandSTR.substring(spaceIndex + 1);
          //Serial.println(extractedString);  //I don't know why,but when you use -o3 must use it
          ROMSave(paddress, pdata, extractedString.c_str());
          break;
        }
      case 'K':
        if(paddress==0xFFFFFFFF)
          iswaitforvpp=false;
        else
          delay(paddress);
        break;
      default:
        Serial.println("No Such Command");
        break;
    }
  }
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize) {
  // Note: SPIFLash Block API: readBlocks/writeBlocks/syncBlocks
  // already include 4K sector caching internally. We don't need to cache it, yahhhh!!
  return flash.readBlocks(lba, (uint8_t *)buffer, bufsize / 512) ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  digitalWrite(LED_BUILTIN, HIGH);
  // Note: SPIFLash Block API: readBlocks/writeBlocks/syncBlocks
  // already include 4K sector caching internally. We don't need to cache it, yahhhh!!
  return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb(void) {
  // sync with flash
  flash.syncBlocks();
  // clear file system's cache to force refresh
  fatfs.cacheClear();
  digitalWrite(LED_BUILTIN, LOW);
}
