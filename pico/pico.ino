#include "hardware/pio.h"
#include <pico.h>
#define PIN_SCL 3u
#define PIN_SDA 2u
#define PIN_SWITCH 4u
#define TIMEOUT 50
PIO pio = pio0;
uint sm;
char pcommandType;
uint32_t paddress;
uint32_t pdata;
uint offset;
static const uint16_t pgm[32] = {
  /*     .wrap_target*/
  0x80a0, /*  0: pull   block*/
  0xe027, /*  1: set    x, 7*/
  0x7001, /*  2: out    pins, 1         side 0*/
  0x1842, /*  3: jmp    x--, 2          side 1*/
  0x00c9, /*  4: jmp    pin, 9*/
  0xe02f, /*  5: set    x, 15*/
  0x7001, /*  6: out    pins, 1         side 0*/
  0x1846, /*  7: jmp    x--, 6          side 1*/
  0x0000, /*  8: jmp    0*/
  0xf880, /*  9: set    pindirs, 0      side 1*/
  0xf02e, /* 10: set    x, 14           side 0*/
  0x5801, /* 11: in     pins, 1         side 1*/
  0x104b, /* 12: jmp    x--, 11         side 0*/
  0x5801, /* 13: in     pins, 1         side 1*/
  0x8000, /* 14: push   noblock*/
  0xe081, /* 15: set    pindirs, 1*/
  0x0000, /* 16: jmp    0*/
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
void pwrite(uint8_t addr, uint16_t data) {
  //Serial.println(data,HEX);
  uint32_t rdata;
  rdata = (addr << 17) | (0x0 << 16) | data;
  pio_sm_put_blocking(pio, sm, rdata << 8);
}
uint16_t pread(uint8_t addr) {
  uint32_t rdata;
  rdata = (addr << 1) | 0x1;
  pio_sm_put_blocking(pio, sm, rdata << 24);
  return static_cast<uint16_t>(pio_sm_get_blocking(pio, sm) & 0xFFFF);
}
void setup() {
  Serial.begin(9600);
  gpio_put(PIN_SWITCH, false);
  pio_init();
}
void loop() {
  if (Serial.available() > 0) {
    String commandSTR = Serial.readStringUntil('\n');
    if (commandSTR.length() > 0) {
      parseString(commandSTR);
      if (pcommandType == 'W') {
        pwrite(paddress, pdata);
      } else if (pcommandType == 'R') {
        uint16_t result = pread(paddress);
        Serial.print("0x");
        Serial.println(result, HEX);
      } else if (pcommandType == 'T') {
        pio->sm[0].clkdiv = paddress;
      } else if (pcommandType == 'A') {
        CSRRead(paddress, pdata);
      } else if (pcommandType == 'E') {
        flasherase(paddress);
      } else if (pcommandType == 'B') {
        beginset();
      } else if (pcommandType == 'F') {
        flashfill(paddress, pdata, 0xFFFF);
      } else if (pcommandType == 'X') {
        uint32_t result = RunCommand(paddress);
        Serial.print("R0:0x");
        Serial.println(result >> 16, HEX);
        Serial.print("EA:0x");
        Serial.println(result & 0xFFFF, HEX);
      } else if (pcommandType == 'C') {
        uint8_t arr[2];
        arr[0] = pdata & 0xFF;
        arr[1] = pdata >> 8;
        flashwrite(paddress, 1, arr);
      } else if (pcommandType == 'D') {
        flasheraseall();
      } else if (pcommandType == 'Q') {
        flashwritemode(paddress);
      } /*else if (pcommandType == 'I') {
        flashwrite(0x0, 0x10000/2, binary_data_0);
        flashwrite(0x10000, 0x10000/2, binary_data_1);
        flashwrite(0x20000, 0x10000/2, binary_data_2);
        flashwrite(0x30000, 0x10000/2, binary_data_3);
      }*/
      Serial.println("Done");
    }
  }
}
void waitack(void) {
  while (pread(0x0c) != 0x00) { Serial.println("ack"); }
}
void rst(void) {
  gpio_put(PIN_SDA, false);
  delayMicroseconds(1);
  gpio_put(PIN_SDA, true);
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
  sm = pio_claim_unused_sm(pio, false);
  offset = pio_add_program(pio, &piopgm);
  i2c_program_init(pio, sm, offset, PIN_SDA, PIN_SCL);
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
void parseString(const String& str) {
  char type;
  int hex[2] = { 0 };
  bool readingType = true;
  int readingHex = 0;
  for (char c : str) {
    if (c == ' ') {
      if (readingType) {
        readingType = false;
        readingHex = 0;
      } else {
        readingHex = readingHex + 1;
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
/*60 mode: 3read 1erase 5write
611:read 5:erase 4 write
62 r 1f or 101f
63 seg
64 addr
65 datain
66 output
67 unlock
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
  while (((value = pread(0x62)) != 0x1F && value != 0x11F) && (j < TIMEOUT)) { j++; }
  if (j >= TIMEOUT) {
    Serial.print("0X62:0x");
    Serial.println(pread(0x62), HEX);
    Serial.println("fail");
  }
  pwrite(0x60, 0x0);
  pwrite(0x61, 0x0);
  delay(50);
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  gpio_put(PIN_SWITCH, false);
}
void flasheraseall() {
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
    uint16_t value;
    while (((value = pread(0x62)) != 0x1F && value != 0x11F) && (j < TIMEOUT)) { j++; }
    if (j >= TIMEOUT) {
      Serial.print("0X62:0x");
      Serial.println(pread(0x62), HEX);
      Serial.println("fail");
    }
    pwrite(0x60, 0x0);
    pwrite(0x61, 0x0);
    delay(50);
  }
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  gpio_put(PIN_SWITCH, false);
  if (j >= TIMEOUT) {
    Serial.println("fail");
  }
  flashfill(0xfc00, 0x100, 0xffff);
}
void flashwrite(uint32_t offset, uint32_t dataSize, const uint8_t* data) {
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
    }
    delayMicroseconds(30);
  }
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  gpio_put(PIN_SWITCH, false);
}
void flashfill(uint32_t offset, uint32_t dataSize, uint16_t data) {
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
    }
    delayMicroseconds(30);
  }
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  gpio_put(PIN_SWITCH, false);
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
        }
        delayMicroseconds(30);
      }
      Serial.println("OK");
    }
  }
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  gpio_put(PIN_SWITCH, false);
}
