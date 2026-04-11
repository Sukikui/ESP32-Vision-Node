# Motion Detection

## Overview

This document describes the PIR motion detection path implemented in the firmware.

The MQTT topic contract remains documented in [`ethernet-mqtt.md`](./ethernet-mqtt.md). Persisted runtime values remain documented in [`runtime-config.md`](./runtime-config.md).

## Configuration Entries

### Build-Time and Default Runtime Values

| Key | Type | Default | Depends on | Purpose |
| --- | --- | --- | --- | --- |
| `APP_HAS_MOTION_DETECTION` | `bool` | `n` | none | Build-time gate for PIR support |
| `APP_MOTION_PIR_GPIO` | `int` | `10` | `APP_HAS_MOTION_DETECTION` | GPIO connected to the PIR digital output |
| `APP_MOTION_PIR_ACTIVE_HIGH` | `bool` | `y` | `APP_HAS_MOTION_DETECTION` | Use rising edge when the PIR drives the line high on motion |
| `APP_MOTION_DEFAULT_ENABLED` | `bool` | `y` | `APP_HAS_MOTION_DETECTION` | Default runtime enabled state used before any persisted override exists |
| `APP_MOTION_DEFAULT_WARMUP_MS` | `int` | `30000` | `APP_HAS_MOTION_DETECTION` | Default warm-up time used before any persisted override exists |
| `APP_MOTION_DEFAULT_COOLDOWN_MS` | `int` | `5000` | `APP_HAS_MOTION_DETECTION` | Default cooldown time used before any persisted override exists |

### Persisted Runtime Values

| Runtime key | NVS key | Feature gate | Meaning |
| --- | --- | --- | --- |
| `motion_detection_enabled` | `motion_en` | `APP_HAS_MOTION_DETECTION` | Enable or disable PIR detection at runtime |
| `motion_warmup_ms` | `motion_wu` | `APP_HAS_MOTION_DETECTION` | Active warm-up time |
| `motion_cooldown_ms` | `motion_cd` | `APP_HAS_MOTION_DETECTION` | Active cooldown time |

If `APP_HAS_MOTION_DETECTION` is disabled:

- the PIR GPIO is not configured
- the motion detection task is not started
- runtime `motion_*` updates are rejected as `unsupported_feature`

## Runtime Model

| Term | Meaning |
| --- | --- |
| `supported` | PIR support is compiled into the firmware build |
| `enabled` | runtime config currently allows PIR detection |
| `started` | the detector is actively running inside the component |
| `armed` | the detector is started and the warm-up window has already elapsed |

## Signal Path

| Step | Component | Action |
| --- | --- | --- |
| 1 | `main.c` | register the motion event handler on the default event loop when PIR support exists |
| 2 | `motion_detection_init()` | configure the GPIO, install the ISR service, add the ISR handler, create the worker task |
| 3 | `motion_detection_start()` | load the active runtime values and decide whether detection starts enabled or disabled |
| 4 | GPIO ISR | wake the worker task only; no timing logic and no MQTT work in interrupt context |
| 5 | worker task | apply warm-up and cooldown filtering |
| 6 | `motion_detection.c` | post `MOTION_DETECTION_EVENT_TRIGGERED` with the accepted trigger timestamp |
| 7 | `main.c` | translate the internal event into `node_event_publish("motion_detected")` |
| 8 | `node_event.c` | publish the MQTT event on `vision/nodes/{node_id}/event` |

## Trigger Filtering

| Parameter | Effect |
| --- | --- |
| `motion_warmup_ms` | Ignore triggers until the detector has been stable long enough after start or re-enable |
| `motion_cooldown_ms` | Ignore triggers that arrive too soon after the previous accepted trigger |

### Warm-up Behavior

| Situation | Behavior |
| --- | --- |
| normal detector start | start a fresh warm-up window |
| detector re-enabled by runtime config | start a fresh warm-up window |
| trigger during warm-up | ignored |

### Cooldown Behavior

| Situation | Behavior |
| --- | --- |
| accepted trigger | record its timestamp |
| next trigger arrives before cooldown expires | ignored |
| next trigger arrives after cooldown expires | accepted |

### Live Runtime Updates

| Update | Effect |
| --- | --- |
| change `motion_warmup_ms` while running | affects future checks only |
| change `motion_cooldown_ms` while running | affects future checks only |
| disable detection | stop the detector state and clear timing state |
| re-enable detection | start again with a fresh warm-up window |

## Internal Interface

### Event Base

| Item | Meaning |
| --- | --- |
| `MOTION_DETECTION_EVENT` | internal ESP event base used by the detector |
| `MOTION_DETECTION_EVENT_TRIGGERED` | event ID posted when one trigger survives filtering |

### Event Payload

| Field | Type | Meaning |
| --- | --- | --- |
| accepted trigger timestamp | `int64_t` | value returned by `esp_timer_get_time()` when the trigger is accepted |

### Public API

| Function | Purpose |
| --- | --- |
| `motion_detection_init()` | configure the GPIO, ISR, and worker task |
| `motion_detection_start()` | apply runtime config and start detection if enabled |
| `motion_detection_apply_runtime_config()` | re-read runtime values and apply them live |
| `motion_detection_is_supported()` | report whether PIR support exists in the current build |
| `motion_detection_is_armed()` | report whether detection is running and past warm-up |
| `motion_detection_get_last_trigger_us()` | return the timestamp of the last accepted trigger |

## MQTT Interaction

| Item | Value |
| --- | --- |
| emitted MQTT event | `motion_detected` |
| emitted topic | `vision/nodes/{node_id}/event` |
| direct MQTT code inside `motion_detection.c` | none |
| runtime MQTT config path | `cmd/config` |

The detector itself does not publish MQTT directly. It emits an internal event, and the application layer bridges that event into the messaging layer.

## Current Limits

| Area | Current behavior |
| --- | --- |
| sensor count | one digital PIR input |
| output events | `motion_detected` only |
| auto-capture | not implemented |
| clear/reset event | not implemented |
| per-trigger metadata | accepted timestamp only |
