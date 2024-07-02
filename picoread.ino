#include <pico.h>
#include "hardware/pio.h"
volatile PIO pio = pio0;
volatile uint sm1, sm2;
uint offset;
static const uint16_t pioread_program_instructions[] = {
  //     .wrap_target
  0x8000,  //  0: push   noblock
  0xe037,  //  1: set    x, 23
  0x2021,  //  2: wait   0 pin, 1
  0x00c8,  //  3: jmp    pin, 8
  0x20a1,  //  4: wait   1 pin, 1
  0x4001,  //  5: in     pins, 1
  0x0042,  //  6: jmp    x--, 2
  0x0000,  //  7: jmp    0
  0xe000,  //  8: set    pins, 0
  0x4001,  //  9: in     pins, 1
  0x0049,  // 10: jmp    x--, 9
  0x0000,  // 11: jmp    0
           //     .wrap
};

static const struct pio_program pioread_program = {
  .instructions = pioread_program_instructions,
  .length = 12,
  .origin = -1,
};
static const uint16_t piotrig_program_instructions[] = {
  //     .wrap_target
  0x2020,  //  0: wait   0 pin, 0
  0x20a0,  //  1: wait   1 pin, 0
  0x00c4,  //  2: jmp    pin, 4
  0x0000,  //  3: jmp    0
  0xe001,  //  4: set    pins, 1
  0x0000,  //  5: jmp    0
           //     .wrap
};
static const struct pio_program piotrig_program = {
  .instructions = piotrig_program_instructions,
  .length = 6,
  .origin = -1,
};
void pio_init(void) {
  sm1 = pio_claim_unused_sm(pio, false);
  offset = pio_add_program(pio, &piotrig_program);
  trig_program_init(pio, sm1, offset, 2u, 3u, 4u);
  sm2 = pio_claim_unused_sm(pio, false);
  offset = pio_add_program(pio, &pioread_program);
  read_program_init(pio, sm2, offset, 2u, 3u, 4u);
}
static inline void read_program_init(PIO pio, uint sm, uint offset, uint pin_sda, uint pin_scl, uint pin_trig) {
  assert(pin_scl == pin_sda + 1);
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_out_pins(&c, pin_trig, 1);
  sm_config_set_set_pins(&c, pin_trig, 1);
  sm_config_set_in_pins(&c, pin_sda);
  sm_config_set_sideset_pins(&c, pin_scl);
  sm_config_set_jmp_pin(&c, pin_trig);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_out_shift(&c, false, false, 24);
  sm_config_set_in_shift(&c, false, false, 24);
  sm_config_set_sideset(&c, 2, true, false);

  sm_config_set_wrap(&c, offset, offset + pioread_program.length - 1);
  float div = (float)1;
  sm_config_set_clkdiv(&c, div);
  pio_gpio_init(pio, pin_sda);
  pio_gpio_init(pio, pin_trig);
  /*gpio_set_oeover(pin_sda, GPIO_OVERRIDE_INVERT);*/
  pio_gpio_init(pio, pin_scl);
  /*gpio_set_oeover(pin_scl, GPIO_OVERRIDE_INVERT);*/
  gpio_pull_up(pin_scl);
  gpio_pull_up(pin_sda);
  gpio_pull_up(pin_trig);
  gpio_put(pin_sda, true);
  gpio_put(pin_scl, true);
  uint32_t both_pins = (1u << pin_sda) | (1u << pin_scl) | (1u << pin_trig);
  pio_sm_set_pindirs_with_mask(pio, sm, (1u << pin_trig), both_pins);
  pio_sm_set_pins_with_mask(pio, sm, both_pins, both_pins);
  pio_sm_init(pio, sm, offset, &c);
  pio_sm_clear_fifos(pio, sm);
  pio_sm_set_enabled(pio, sm, true);
}
static inline void trig_program_init(PIO pio, uint sm, uint offset, uint pin_sda, uint pin_scl, uint pin_trig) {
  assert(pin_scl == pin_sda + 1);
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_out_pins(&c, pin_trig, 1);
  sm_config_set_set_pins(&c, pin_trig, 1);
  sm_config_set_in_pins(&c, pin_sda);
  sm_config_set_sideset_pins(&c, pin_scl);
  sm_config_set_jmp_pin(&c, pin_scl);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_out_shift(&c, false, false, 24);
  sm_config_set_in_shift(&c, false, false, 24);
  sm_config_set_sideset(&c, 2, true, false);

  sm_config_set_wrap(&c, offset, offset + piotrig_program.length - 1);
  float div = (float)1;
  sm_config_set_clkdiv(&c, div);
  pio_gpio_init(pio, pin_sda);
  pio_gpio_init(pio, pin_trig);
  /*gpio_set_oeover(pin_sda, GPIO_OVERRIDE_INVERT);*/
  pio_gpio_init(pio, pin_scl);
  /*gpio_set_oeover(pin_scl, GPIO_OVERRIDE_INVERT);*/
  gpio_pull_up(pin_scl);
  gpio_pull_up(pin_sda);
  gpio_pull_up(pin_trig);
  gpio_put(pin_sda, true);
  gpio_put(pin_scl, true);
  uint32_t both_pins = (1u << pin_sda) | (1u << pin_scl) | (1u << pin_trig);
  pio_sm_set_pindirs_with_mask(pio, sm, (1u << pin_trig), both_pins);
  pio_sm_set_pins_with_mask(pio, sm, both_pins, both_pins);
  pio_sm_init(pio, sm, offset, &c);
  pio_sm_clear_fifos(pio, sm);
  pio_sm_set_enabled(pio, sm, true);
}
#define QUEUE_LENGTH 4096

struct MyQueue {
  uint32_t data[QUEUE_LENGTH];
  int head;
  int tail;
};

volatile MyQueue queue = { {}, 0, 0 };

void enqueue(volatile struct MyQueue *q, uint32_t value) {
  int nextTail = (q->tail + 1) % QUEUE_LENGTH;
  if (nextTail != q->head) {
    q->data[q->tail] = value;
    q->tail = nextTail;
  } else {
    Serial.println("Queue full, data lost");
  }
}

bool dequeue(volatile struct MyQueue *q, uint32_t *value) {
  if (q->head != q->tail) {
    *value = q->data[q->head];
    q->head = (q->head + 1) % QUEUE_LENGTH;
    return true;
  } else {
    return false;
  }
}
void loop1() {

  if (!pio_sm_is_rx_fifo_empty(pio, sm2)) {
    uint32_t data = pio_sm_get(pio, sm2);
    enqueue(&queue, data);
  } else {
    if (queue.head != queue.tail) {
      uint32_t data_to_send = queue.data[queue.head];
      if (rp2040.fifo.push_nb(data_to_send)) {
        queue.head = (queue.head + 1) % QUEUE_LENGTH;
      } else {
        return;
      }
    } else {
      return;
    }
  }
}
void setup() {
  Serial.begin(9600);
  pio_init();
}

void loop() {
  Serial.println(rp2040.fifo.pop(), HEX);
}
