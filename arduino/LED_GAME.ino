/* Autoren: Alexey Galakhov und Michael Springwald
 * Datum: Samstag den 24.11.2018
 * 
 * Steuert 2 10mm RGB(Gemeinsame Anode) über 3 Poti's. 
 * Es gibt noch einen Button(Kurzhubtaster) über den man in den Spielemodus gelangt.
 * 
 * In diesem Coden nutzen wir SoftPWM weil der atTiny84 nur 4 PWM Pins hat, die je zwei Unterschiedliche Timer zugeordnet sind.
 * Einmal ein 8 Bit Timer nd einmal ein 16 Bit Timer. 
 * 
 * Es wird hier direkt auf die Register vom MC zugegriffen ohne Funktionen wie AnalogRead oder so zu verwenden.
 * Weil AnalogRead dauert einfach zu lange, in der Zeit wird dann SoftPWM gemacht.
 * Auch DigitalWrite wird hier nicht genutzt, weil es ebenfalls zu lange dauert und weil wir so die Pins nicht "gleichzeitig" setzten können.
 * 
 */

// PB1 = button
// PB2 = 1 blue
// PA7 = 1 green
// PA6 = 1 red
// PA5 = 2 blue
// PA4 = 2 green
// PA3 = 2 red

/* Erster Modus LED Spiel
 * Manuell Farben einstlelen
 * Zufallsfarben
 */
enum Mode {
  RandomColors = 0,
  ManualColor,
  LedGame,

  _MAX_MODE
};

struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

static inline Color
color_of_mode(enum Mode mode) {
  switch (mode) {
    case RandomColors: return Color { 255, 0, 0 };
    case ManualColor:  return Color { 0, 255, 0 };
    case LedGame:      return Color { 0, 0, 255 };
    default:           return Color { 255, 255, 255 };
  }
}

static inline bool
compare_colors(struct Color color1, struct Color color2, uint8_t dist) {
  return abs((int8_t)(color1.red - color2.red)) < dist
      && abs((int8_t)(color1.green - color2.green)) < dist
      && abs((int8_t)(color1.blue - color2.blue)) < dist
      ;
}

static inline uint8_t
led_bit(uint8_t cycle, uint8_t value, size_t bit) {
    return ((cycle < value) ? 0 : 1) << bit;
}

static inline void
set_leds(uint8_t cycle, struct Color color1, struct Color color2) {
    uint8_t portb = led_bit(cycle, color2.blue,  2);
    uint8_t porta = led_bit(cycle, color2.green, 7)
                  | led_bit(cycle, color2.red,   6)
                  | led_bit(cycle, color1.blue,  5)
                  | led_bit(cycle, color1.green, 4)
                  | led_bit(cycle, color1.red,   3);
    PORTA = porta;
    PORTB = portb;
}

static inline struct Color
rand_color(void) {
    struct Color color = { (uint8_t)rand(), (uint8_t)rand(), (uint8_t)rand() };
    return color;
}

static bool
button_down(void) {
  return !(PINB & 0x02);
}

static bool
pot4_pos(void) {
  return (PING & 0x01);
}

struct AdcState {
  uint8_t pot1;
  uint8_t pot2;
  uint8_t pot3;
  uint8_t pot4;
  uint8_t adc_channel;
};

static void
adc_read(struct AdcState *state) {
  if (! (ADCSRA & (1 << ADSC))) { // ADC conversion is complete
    uint8_t adc_value = ADCH;
    switch (state->adc_channel) {
      case 0:
        state->pot1 = adc_value;
        break;
      case 1:
        state->pot2 = adc_value;
        break;
      case 2:
        state->pot3 = adc_value;
        break;
      case 3:
        state->pot4 = adc_value;
        break;
    }
    state->adc_channel = (state->adc_channel + 1) & 0x3;
    ADMUX = state->adc_channel;
    ADCSRA |= (1 << ADSC); // start ADC conversion
  }
}

struct Leds {
  struct Color color1;
  struct Color color2;
};

struct RandomColorsState {
  uint32_t last_change1;
  uint32_t last_change2;
};

struct LedGameState {
  struct Color blink_color;
  uint32_t blink_timer;
  uint8_t blink_count;
};

struct GlobalState {
  enum Mode mode;
  uint16_t mode_switch;
  struct AdcState adc_state;
  struct Leds leds;
  uint32_t cycle;
  struct RandomColorsState random_colors_state;
  struct LedGameState led_game_state;
};

static void
random_colors(struct GlobalState *state) {
  uint32_t left_speed = ((uint32_t)(0xFF - state->adc_state.pot1) << 7) + 1024;
  uint32_t right_speed = ((uint32_t)(0xFF - state->adc_state.pot3) << 7) + 1024;
  if (state->cycle - state->random_colors_state.last_change1 > left_speed) {
     state->random_colors_state.last_change1 = state->cycle;
     state->leds.color1 = rand_color();
  }
  if (state->cycle - state->random_colors_state.last_change2 > right_speed) {
     state->random_colors_state.last_change2 = state->cycle;
     state->leds.color2 = rand_color();
  }
}

static void
manual_color(struct GlobalState *state) {
  struct Color color = {
    state->adc_state.pot1,
    state->adc_state.pot2,
    state->adc_state.pot3
  };
  state->leds = Leds { color, color };
}

static void
led_game(struct GlobalState *state) {
  if (state->led_game_state.blink_count > 0) {
    if (state->led_game_state.blink_timer == state->cycle) {
      state->led_game_state.blink_timer = state->cycle + 4096;
      if (state->led_game_state.blink_count & 1) {
        state->leds = Leds { { 0, 0, 0 }, {0, 0, 0} };
      } else {
        state->leds = Leds { state->led_game_state.blink_color, state->led_game_state.blink_color };
      }
      --state->led_game_state.blink_count;
      if (! state->led_game_state.blink_count) {
        state->leds.color2 = rand_color();
      }
    }
  } else {
    struct Color player_color = {
      state->adc_state.pot1,
      state->adc_state.pot2,
      state->adc_state.pot3,
    };
    state->leds.color1 = player_color;

    uint8_t easiness = pot4_pos() ? 64 : 48;
    bool hit = compare_colors(state->leds.color1, state->leds.color2, easiness);
    if (hit) {
      state->led_game_state = LedGameState {
        state->leds.color2, /* color */
        state->cycle + 4096, /* timer */
        9 /* count */
      };
    }
  }
}

static struct GlobalState state = {
  .mode = RandomColors,
  .mode_switch = 0,
  .adc_state = {
    .pot1 = 0,
    .pot2 = 0,
    .pot3 = 0,
    .pot4 = 0,
    .adc_channel = 0,
  },
  .leds = {
    .color1 = { 0, 0, 0 },
    .color2 = { 0, 0, 0 },
  },
  .cycle = 0,
  .random_colors_state = { 0, 0 },
};

void 
setup() {
  DDRA = 0xF8;
  DDRB = 0x04;

  ADCSRB = 0x10; // ADC left adjust
  ADCSRA = 0x84; // ADC at 1/16 speed
}


void
loop() {
  adc_read(&state.adc_state);

  if (button_down()) {
    state.mode_switch = 4096;
  } else {
    if (state.mode_switch > 1) {
      --state.mode_switch;
    }
  }

  if (state.mode_switch == 1) {
    state.mode = (enum Mode)((state.mode + 1) % _MAX_MODE);
    --state.mode_switch;
  }

  if (state.mode_switch) {
    struct Color color = color_of_mode(state.mode);
    state.leds = Leds { color, color };
  } else {
    switch (state.mode) {
      case RandomColors: random_colors(&state); break;
      case ManualColor:  manual_color(&state);  break;
      case LedGame:      led_game(&state);      break;
      default: break;
    }
  }

  set_leds(state.cycle, state.leds.color1, state.leds.color2);
  ++state.cycle;
}

