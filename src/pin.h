
typedef enum {
  PIN_ENTER,
  PIN_SET,
  PIN_CONFIRM
} PinState;

uint8_t display_pin_entry_screen(PinState pin_state, uint8_t attempts_left, uint8_t* result) BANKED;