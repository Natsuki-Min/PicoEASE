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
unsigned char buf[4096];
void pwrite(uint8_t addr, uint16_t data) {
  //Serial.println(data,HEX);
  rst();
  uint32_t rdata;
  rdata = (addr << 17) | (0x0 << 16) | data;
  pio_sm_put_blocking(pio, sm, rdata << 8);
}
uint16_t pread(uint8_t addr) {
  rst();
  uint32_t rdata;
  rdata = (addr << 1) | 0x1;
  pio_sm_put_blocking(pio, sm, rdata << 24);
  return static_cast<uint16_t>(pio_sm_get_blocking(pio, sm) & 0xFFFF);
}
void setup() {
  Serial.begin(115200);
  gpio_put(PIN_SWITCH, false);
  pio_init();
}
void loop() {
  if (Serial.available() > 0) {
    String commandSTR = Serial.readStringUntil('\n'); 
    if(commandSTR == "W") {
        while(Serial.available()) Serial.read(); //clean buffer
        pwrite(0x67, 0x01);
        pwrite(0x60, 0x05);
        pwrite(0x61, 0x00);
        for(uint64_t i = 0; i <= 3; i++) {
          pwrite(0x63, i);
          pwrite(0x64, 0);
          uint16_t value=0;
          for(uint64_t j = 0; j < 16; j++) {
            Serial.write(0xAC);
            Serial.readBytes(buf, 4096);
            for(int n = 0; n < 2048; n++) {
              //Serial.write(buf[2*n]);
              //Serial.write(buf[2*n+1]);
              pwrite(0x65, ((uint16_t)(buf[2*n+1]) << 8) | (uint16_t)(buf[2*n]));
              pwrite(0x61, 0x04);
              while (pread(0x61) != 0x4) {tight_loop_contents();}
              while ((value = pread(0x62)) != 0x1F && value != 0x101F) {tight_loop_contents();}
            }
          }
        }
        Serial.write(0xAC);
      }
    if (commandSTR.length() == 5) {
      if(commandSTR[0] == 'B' && commandSTR[1] == 'W') {
        pwrite(commandSTR[2], (commandSTR[3]<<8)|commandSTR[4]);
      }
    }
    else if (commandSTR.length() == 3) {
      if(commandSTR[0] == 'B' && commandSTR[1] == 'R') {
        uint16_t data = pread(commandSTR[2]);
        uint8_t datH = data >> 8, datL = data & 0xff;
        Serial.write(datH);
        Serial.write(datL);
      }
    }
    else if (commandSTR.length() == 12) {
      //FFxxxx xxxx xx
      if(commandSTR[0] == 'F' && commandSTR[1] == 'F') {
      uint32_t offset = ((uint32_t)(commandSTR[2]) << 24) | ((uint32_t)(commandSTR[3]) << 16) | ((uint32_t)(commandSTR[4]) << 8) | (uint32_t)(commandSTR[5]);
      uint32_t size = ((uint32_t)(commandSTR[6]) << 24) | ((uint32_t)(commandSTR[7]) << 16) | ((uint32_t)(commandSTR[8]) << 8) | (uint32_t)(commandSTR[9]);
      uint16_t dat = ((uint16_t)(commandSTR[10]) << 8) | (uint16_t)(commandSTR[11]);

        flashfill(offset, size, dat);
      }
    }
    else if (commandSTR.length() == 1) {
      parseString(commandSTR);
      if (pcommandType == 'V') {
        Serial.println("0.03");
      } else if(pcommandType == 'B') {
          gpio_put(PIN_SCL, true);
          gpio_put(PIN_SDA, false);
          delayMicroseconds(10);
          gpio_put(PIN_SDA, true);
      } else if(pcommandType == 'R') {
         //read rom
        if (pread(0x67) != 0x00) pwrite(0x67, 0x0000);//读取模式
        pwrite(0x60, 0x0003);
        for (int seg = 0; seg <= 3; seg++)
        {
          pwrite(0x63, seg);//seg地址
          pwrite(0x64, 0x0000);//后四位
          for (int i = 0; i < (0x10000 >> 1); i++)
          {
              pwrite(0x61, 0x0001);
              uint16_t x = pread(0x66);
              Serial.write(x & 0xff);
              Serial.write(x >> 8);
          }
        }
        pwrite(0x60, 0x0000);
        pwrite(0x61, 0x0000);
      }
      //Serial.println("Done");
    }
  }
}
void flashfill(uint32_t offset, uint32_t dataSize, uint16_t data) {
  dataSize /= 2;
  rst();
  if (pread(0x67) == 0x0) {
    pwrite(0x67, 0x1); /*unlock*/
  }
  pwrite(0x60, 0x5); /*enter write mode*/
  pwrite(0x63, offset >> 16);
  pwrite(0x64, offset & 0xFFFF); /*write addr*/
  int j;
  for (int i = 0; i < dataSize; i++) {
    pwrite(0x65, data);
    pwrite(0x61, 0x4); /*begin*/
    j = 0;
    while (pread(0x61) != 0x4)  delay(5);
    uint16_t value;
    while ((value = pread(0x62)) != 0x1F && value != 0x101F) delay(5);
    delayMicroseconds(30);
  }
  if (pread(0x67) == 0x1) {
    pwrite(0x67, 0x0); /*lock*/
  }
  Serial.write(0xAC);
}
void rst(void) {
  gpio_put(PIN_SDA, false);
  delayMicroseconds(1);
  gpio_put(PIN_SDA, true);
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
