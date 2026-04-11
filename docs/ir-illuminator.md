# IR Illuminator

## Overview

This document describes the optional IR illuminator control path implemented in the firmware.

It covers:

- the hardware model assumed by the firmware
- the difference between build-time support and runtime mode
- the currently supported IR control policies
- the internal firmware API used to drive the illuminator
- how MQTT runtime configuration maps to the IR output

The MQTT command topic contract remains documented in [`ethernet-mqtt.md`](./ethernet-mqtt.md). This document focuses on the local node-side behavior of the illuminator itself.

## Hardware Model

The firmware treats the IR illuminator as a simple GPIO-controlled actuator.

The expected wiring is:

- `VCC` and `GND` power the illuminator or its driver stage
- one GPIO controls either:
  - an `EN` or `IN` input exposed by the illuminator module
  - a transistor or MOSFET gate that switches the illuminator power

The ESP32 does not assume that the GPIO powers the illuminator directly.

This matters because many IR illuminators draw more current than one GPIO pin can source safely.

## Build-Time Support

IR illuminator support is gated at build time through `menuconfig`:

| Key | Meaning |
| --- | --- |
| `APP_HAS_IR_ILLUMINATOR` | compile IR illuminator support into this firmware build |
| `APP_IR_ILLUMINATOR_GPIO` | GPIO connected to the illuminator enable/input line |
| `APP_IR_ILLUMINATOR_ACTIVE_HIGH` | drive the GPIO high to turn the illuminator on |
| `APP_IR_ILLUMINATOR_DEFAULT_MODE` | default runtime policy used before any NVS override exists |

If `APP_HAS_IR_ILLUMINATOR` is disabled:

- the GPIO is not configured
- runtime MQTT updates for `ir_illuminator_mode` are rejected as `unsupported_feature`
- the live runtime mode is treated as `off`

## Runtime Mode

The IR illuminator currently supports three runtime modes:

| Mode | Meaning |
| --- | --- |
| `off` | keep the illuminator off |
| `on` | keep the illuminator on continuously |
| `capture` | turn the illuminator on only while a capture session is marked active |

This mode is persisted in NVS through `runtime_config`.

In practice:

- `off` is the safest idle policy
- `on` is useful for bring-up or fixed night scenes
- `capture` is the forward-looking policy intended for future camera integration

## Firmware Flow

The control path is intentionally simple:

1. `runtime_config_init()` restores the persisted IR mode from NVS, or falls back to `APP_IR_ILLUMINATOR_DEFAULT_MODE`.
2. `main.c` calls `ir_illuminator_init()` to configure the GPIO as an output.
3. `main.c` calls `ir_illuminator_apply_runtime_config()` to drive the output according to the restored mode.
4. Later, `cmd/config` can update the mode and persist it.
5. After a successful config update, `command_router.c` calls `ir_illuminator_apply_runtime_config()` so the live output changes immediately.

The component keeps one internal `capture_active` flag so future camera code can signal when a capture is running.

## Current API

The public API exposed by `ir_illuminator.h` is:

- `ir_illuminator_init()`
- `ir_illuminator_apply_runtime_config()`
- `ir_illuminator_set_capture_active(bool capture_active)`
- `ir_illuminator_is_supported()`
- `ir_illuminator_is_on()`

The intended split is:

- `runtime_config` owns the persisted mode
- `ir_illuminator` owns the GPIO output
- future camera code will own the capture lifecycle and call `ir_illuminator_set_capture_active(...)`

## MQTT Runtime Control

The illuminator is controlled through the existing `cmd/config` command.

Example payload:

```json
{
  "request_id": "req-60",
  "ir_illuminator_mode": "on"
}
```

Another example:

```json
{
  "request_id": "req-61",
  "ir_illuminator_mode": "capture"
}
```

Current behavior:

- the mode is validated before persistence
- valid updates are committed to NVS
- the live GPIO output is updated immediately after a successful commit
- unsupported builds reject the key as `unsupported_feature`

## Capture Coupling

The camera pipeline does not exist yet, but the IR component is already prepared for it.

The intended future sequence is:

1. camera code calls `ir_illuminator_set_capture_active(true)`
2. the illuminator turns on if the runtime mode is `capture`
3. image capture happens
4. camera code calls `ir_illuminator_set_capture_active(false)`
5. the illuminator turns off again

This means the illuminator policy is already implemented, even though the actual camera capture path is still pending.

## Related Documents

- [`ethernet-mqtt.md`](./ethernet-mqtt.md)
- [`runtime-config.md`](./runtime-config.md)
