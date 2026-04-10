# ESP32-Vision-Node

🚧 Under construction 🚧

## Project Context

The local files `.vscode/settings.json` and `.vscode/c_cpp_properties.json` should stay consistent with this context.

| Item | Value |
| --- | --- |
| Board | `Waveshare ESP32-P4-POE-ETH` |
| Chip | `ESP32-P4` |
| Target | `esp32p4` |
| Architecture | `RISC-V` |
| Toolchain | `riscv32-esp-elf-gcc` |
| OpenOCD board config | `board/esp32p4-builtin.cfg` |

## Documentation

- [`docs/ethernet-mqtt.md`](./docs/ethernet-mqtt.md)
  Ethernet, MQTT topics, boot flow, and image transport.
- [`docs/motion-detection.md`](./docs/motion-detection.md)
  PIR behavior, trigger flow, warm-up, and cooldown.
- [`docs/runtime-config.md`](./docs/runtime-config.md)
  Runtime settings, NVS persistence, and reboot behavior.
