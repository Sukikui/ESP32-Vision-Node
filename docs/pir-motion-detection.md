# PIR Motion Detection

## Overview

This document describes how the optional PIR motion detection path works in the firmware.

It covers:

- the hardware model assumed by the firmware
- the difference between build-time support and runtime enablement
- the internal detection flow from GPIO interrupt to MQTT event
- the warm-up and cooldown timing model
- the runtime parameters that can be changed through `cmd/config`

The MQTT topic contract itself is documented in [`ethernet-mqtt-architecture.md`](./ethernet-mqtt-architecture.md). This document focuses on the local node-side behavior.

## Hardware Model

The current implementation assumes one PIR module connected to one GPIO digital input.

Important characteristics:

- the PIR is treated as a binary trigger source, not as an analog sensor
- one edge on the configured GPIO means "the PIR reported motion"
- the expected active edge depends on the module polarity
- the firmware currently supports one PIR input only

The build-time hardware mapping is defined through `menuconfig`:

| Key | Meaning |
| --- | --- |
| `APP_HAS_MOTION_DETECTION` | compile PIR support into this firmware build |
| `APP_MOTION_PIR_GPIO` | GPIO connected to the PIR digital output |
| `APP_MOTION_PIR_ACTIVE_HIGH` | use rising edge when the PIR drives the line high on motion |

If `APP_HAS_MOTION_DETECTION` is disabled:

- the PIR GPIO is not configured
- the motion detection task is not started
- the event bridge in `main.c` is not registered
- runtime MQTT updates for `motion_*` settings are rejected as `unsupported_feature`

## Build-Time Support vs Runtime Enable

The PIR feature has two layers that must stay separate.

| Layer | Key | Meaning |
| --- | --- | --- |
| Build-time support | `APP_HAS_MOTION_DETECTION` | this firmware build includes PIR support and expects PIR-related hardware configuration |
| Build-time default | `APP_MOTION_DEFAULT_ENABLED` | default runtime state used on first boot before any NVS override exists |
| Build-time default | `APP_MOTION_DEFAULT_WARMUP_MS` | default warm-up duration used on first boot before any NVS override exists |
| Build-time default | `APP_MOTION_DEFAULT_COOLDOWN_MS` | default cooldown duration used on first boot before any NVS override exists |
| Runtime override | `motion_detection_enabled` | persisted flag that enables or disables PIR detection at runtime |
| Runtime override | `motion_warmup_ms` | persisted warm-up override |
| Runtime override | `motion_cooldown_ms` | persisted cooldown override |

In short:

- `APP_HAS_MOTION_DETECTION` answers "does this firmware build support PIR at all?"
- `motion_detection_enabled` answers "if PIR support exists, should detection currently run?"

This separation avoids mixing:

- hardware and build capabilities
- runtime operating state

## Detection Flow

The internal flow is intentionally split into small steps so the GPIO interrupt stays minimal and MQTT stays outside the detector.

1. `main.c` creates the default event loop and registers a motion event handler only when `motion_detection_is_supported()` is true.
2. `motion_detection_init()` validates the configured GPIO, configures it as an input, installs the GPIO ISR service, adds the PIR ISR handler, and creates one worker task.
3. The ISR does not publish MQTT messages and does not run timing logic. It only wakes the worker task with a task notification.
4. `motion_detection_start()` reads the active runtime settings from `runtime_config` and decides whether the detector should start armed or disabled.
5. The worker task waits for GPIO-trigger notifications.
6. When a trigger arrives, the worker applies the warm-up and cooldown rules before accepting it.
7. When a trigger is accepted, `motion_detection.c` posts `MOTION_DETECTION_EVENT_TRIGGERED` on the default ESP event loop with the accepted trigger timestamp.
8. The handler in `main.c` receives that internal event and calls `node_event_publish("motion_detected")`.
9. `node_event_publish()` publishes the MQTT event on `vision/nodes/{node_id}/event`.

This split keeps responsibilities clear:

- `motion_detection.c` knows GPIO, timing, and trigger filtering
- `main.c` bridges the internal event into the messaging layer
- `node_event.c` owns the MQTT event publication

## Warm-up and Cooldown

The PIR path uses two timing windows:

| Parameter | Meaning |
| --- | --- |
| `motion_warmup_ms` | time after startup or re-enable during which triggers are ignored |
| `motion_cooldown_ms` | minimum delay between two accepted triggers |

### Warm-up

Warm-up exists because many PIR modules are unstable just after they are powered or re-armed.

During warm-up:

- GPIO edges may still happen
- the worker task still wakes up
- but every trigger is ignored until the warm-up deadline is reached

The current implementation starts a fresh warm-up window:

- on normal detector start
- when runtime config re-enables the detector after it was disabled

### Cooldown

Cooldown prevents one physical motion event from producing several MQTT events in a very short time.

During cooldown:

- the detector remembers the timestamp of the last accepted trigger
- a new edge is ignored if it arrives too soon after that timestamp

### Live updates

Runtime updates behave like this:

- changing `motion_warmup_ms` or `motion_cooldown_ms` while the detector is already running updates future checks
- changing those values does not reset the current warm-up window or the last accepted trigger timestamp
- disabling detection clears the current started state
- re-enabling detection starts a fresh warm-up window

## Runtime Parameters

The PIR-related runtime keys are updated through the same MQTT `cmd/config` command described in [`ethernet-mqtt-architecture.md`](./ethernet-mqtt-architecture.md).

Example payload:

```json
{
  "request_id": "req-42",
  "motion_detection_enabled": true,
  "motion_warmup_ms": 30000,
  "motion_cooldown_ms": 5000
}
```

Current behavior:

- missing keys leave the current runtime value unchanged
- unknown keys are ignored
- supported keys are validated together before persistence
- valid updates are stored in NVS and then applied to the live detector
- if PIR support is not compiled into the current build, the `motion_*` block is rejected as `unsupported_feature`

The persistence model itself is documented in [`runtime-config-nvs.md`](./runtime-config-nvs.md).

## Internal Events

The detector publishes one internal event base:

- `MOTION_DETECTION_EVENT`

And one event ID:

- `MOTION_DETECTION_EVENT_TRIGGERED`

The event payload is a single `int64_t` timestamp in microseconds, taken from `esp_timer_get_time()` when the trigger is accepted.

This event is internal to the firmware. It is not itself an MQTT payload.

## MQTT Interaction

The detector does not publish MQTT directly.

Its MQTT-visible effect is:

- one accepted PIR trigger becomes one `motion_detected` node event

This means:

- PIR logic stays independent from MQTT client code
- the same internal event could later trigger other actions such as image capture without rewriting the detector itself

The MQTT topic, payload shape, and surrounding control-plane behavior remain documented in [`ethernet-mqtt-architecture.md`](./ethernet-mqtt-architecture.md).

## Current Limitations

The current implementation is intentionally narrow:

- one digital PIR input only
- no auto-capture behavior yet
- no separate `motion_cleared` event
- no per-trigger metadata beyond the accepted timestamp inside the internal event
- no dedicated MQTT reset command for PIR settings yet

This keeps the PIR path small and predictable while the rest of the vision stack is still evolving.
