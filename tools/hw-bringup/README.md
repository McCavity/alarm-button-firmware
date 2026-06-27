# Hardware self-test probe

A standalone sketch (not the firmware) to verify the wiring of an Axiometa Genesis Mini —
useful when first assembling a board or if you wire the modules into different slots.

```bash
pio run -e bringup -t upload     # build + flash the probe
pio device monitor -e bringup    # serial console @115200, press 'h' for help
```

It runs two things at once:

- **Input scan** — the encoder/button candidate pins are `INPUT_PULLUP` and printed live
  whenever one changes. Rotate the encoder / press the button: the GPIOs that move are the
  inputs (the ones that never move are LED outputs).
- **Output drive** — serial commands blink LED candidates, beep buzzer candidates, and sweep
  the LCD's CS/DC/RST permutations, ST7735 driver tab, rotation and colour inversion.

The wiring this confirmed for the reference board is recorded in the repo `CLAUDE.md`
("Module layout"). If your board differs, this probe is how you re-derive it.
