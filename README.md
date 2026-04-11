# ESP32-Vision-Node

🚧 Under construction 🚧

## Project Context

Local editor, debugger, and toolchain settings should stay aligned with this project context.

| Item | Value |
| --- | --- |
| Board | `Waveshare ESP32-P4-POE-ETH` |
| Chip | `ESP32-P4` |
| Target | `esp32p4` |
| Architecture | `RISC-V` |
| Toolchain | `riscv32-esp-elf-gcc` |
| OpenOCD board config | `board/esp32p4-builtin.cfg` |

## Official References

- [Waveshare ESP32-P4-ETH Wiki](https://www.waveshare.com/wiki/ESP32-P4-ETH)
  Official board documentation from Waveshare.
- [ESP32-P4 Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/about.html)
  Official ESP32-P4 documentation in the ESP-IDF Programming Guide.
- [ESP32-P4 Datasheet](https://documentation.espressif.com/esp32-p4_datasheet_en.html)
  Official chip datasheet from Espressif.
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/index.html)
  Official framework documentation for ESP-IDF on ESP32-P4.

## Documentation

- [`docs/ethernet-mqtt.md`](./docs/ethernet-mqtt.md)
  Ethernet, MQTT topics, boot flow, and image transport.
- [`docs/motion-detection.md`](./docs/motion-detection.md)
  PIR behavior, trigger flow, warm-up, and cooldown.
- [`docs/ir-illuminator.md`](./docs/ir-illuminator.md)
  IR illuminator wiring, runtime modes, and future capture coupling.
- [`docs/kconfig.md`](./docs/kconfig.md)
  Build-time settings, generated config flow, and how Kconfig relates to runtime config.
- [`docs/runtime-config.md`](./docs/runtime-config.md)
  Runtime settings, NVS persistence, and reboot behavior.
