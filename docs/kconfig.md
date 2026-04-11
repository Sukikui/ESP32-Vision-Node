# Kconfig

## Overview

This document describes the configuration entries defined in:

- `components/app_config/Kconfig.projbuild`

Configuration flow:

1. values are selected in `menuconfig`
2. ESP-IDF writes them into `sdkconfig`
3. ESP-IDF generates `sdkconfig.h`
4. `components/app_config/include/app_config.h` exposes the `APP_*` macros used by the firmware

## Configuration Entries

### Node and Network

| Key | Type | Default | Depends on | Purpose |
| --- | --- | --- | --- | --- |
| `APP_NODE_ID` | `string` | `p4-001` | none | Node identifier used in MQTT topics and payloads |
| `APP_HEARTBEAT_INTERVAL_S` | `int` | `30` | none | Default heartbeat interval; used until a persisted runtime override exists |
| `APP_MQTT_BROKER_PORT` | `int` | `1883` | none | TCP port used to build the MQTT broker URI from the DHCP gateway IP |

### Buffer and Transport Limits

| Key | Type | Default | Depends on | Purpose |
| --- | --- | --- | --- | --- |
| `APP_TOPIC_MAX_LEN` | `int` | `128` | none | Maximum MQTT topic buffer length |
| `APP_JSON_PAYLOAD_MAX_LEN` | `int` | `384` | none | Maximum size of small JSON payload buffers |
| `APP_REQUEST_ID_MAX_LEN` | `int` | `64` | none | Maximum request ID length extracted from command payloads |
| `APP_PUBLISH_QUEUE_LENGTH` | `int` | `12` | none | Maximum number of queued outgoing MQTT messages |
| `APP_PUBLISH_MAX_DATA_LEN` | `int` | `2048` | none | Maximum payload size stored in one queued MQTT message |
| `APP_IMAGE_CHUNK_SIZE` | `int` | `2048` | none | Maximum number of bytes sent in one image chunk; must stay `<= APP_PUBLISH_MAX_DATA_LEN` |

### PIR Motion Detection

| Key | Type | Default | Depends on | Purpose |
| --- | --- | --- | --- | --- |
| `APP_HAS_MOTION_DETECTION` | `bool` | `n` | none | Build-time gate for PIR support |
| `APP_MOTION_PIR_GPIO` | `int` | `10` | `APP_HAS_MOTION_DETECTION` | GPIO connected to the PIR digital output |
| `APP_MOTION_PIR_ACTIVE_HIGH` | `bool` | `y` | `APP_HAS_MOTION_DETECTION` | Use rising edge when the PIR drives the line high on motion |
| `APP_MOTION_DEFAULT_ENABLED` | `bool` | `y` | `APP_HAS_MOTION_DETECTION` | Default runtime enabled state; used until a persisted override exists |
| `APP_MOTION_DEFAULT_WARMUP_MS` | `int` | `30000` | `APP_HAS_MOTION_DETECTION` | Default PIR warm-up time; used until a persisted override exists |
| `APP_MOTION_DEFAULT_COOLDOWN_MS` | `int` | `5000` | `APP_HAS_MOTION_DETECTION` | Default PIR cooldown time; used until a persisted override exists |

### IR Illuminator

| Key | Type | Default | Depends on | Purpose |
| --- | --- | --- | --- | --- |
| `APP_HAS_IR_ILLUMINATOR` | `bool` | `n` | none | Build-time gate for IR illuminator support |
| `APP_IR_ILLUMINATOR_GPIO` | `int` | `11` | `APP_HAS_IR_ILLUMINATOR` | GPIO connected to the illuminator enable/input line |
| `APP_IR_ILLUMINATOR_ACTIVE_HIGH` | `bool` | `y` | `APP_HAS_IR_ILLUMINATOR` | Drive the GPIO high to turn the illuminator on |
| `APP_IR_ILLUMINATOR_DEFAULT_MODE` | `choice` | `off` | `APP_HAS_IR_ILLUMINATOR` | Default runtime IR policy; used until a persisted override exists |

`APP_IR_ILLUMINATOR_DEFAULT_MODE` has three possible values:

| Choice entry | Meaning |
| --- | --- |
| `APP_IR_ILLUMINATOR_DEFAULT_MODE_OFF` | illuminator off |
| `APP_IR_ILLUMINATOR_DEFAULT_MODE_ON` | illuminator always on |
| `APP_IR_ILLUMINATOR_DEFAULT_MODE_CAPTURE` | illuminator on only while a capture session is marked active |

## Runtime Interaction

Some `Kconfig` entries are only defaults for values that can later be overridden in NVS.

| Kconfig default | Runtime key | Notes |
| --- | --- | --- |
| `APP_HEARTBEAT_INTERVAL_S` | `heartbeat_interval_s` | Runtime value wins if already persisted in NVS |
| `APP_MOTION_DEFAULT_ENABLED` | `motion_detection_enabled` | Runtime value wins if already persisted in NVS |
| `APP_MOTION_DEFAULT_WARMUP_MS` | `motion_warmup_ms` | Runtime value wins if already persisted in NVS |
| `APP_MOTION_DEFAULT_COOLDOWN_MS` | `motion_cooldown_ms` | Runtime value wins if already persisted in NVS |
| `APP_IR_ILLUMINATOR_DEFAULT_MODE` | `ir_illuminator_mode` | Runtime value wins if already persisted in NVS |

If no persisted runtime value exists, the firmware falls back to the `Kconfig` default compiled into the image.

If a persisted runtime value already exists, flashing a new firmware with new `Kconfig` defaults does not replace that runtime value automatically.

## Practical Use

Use `menuconfig` when you want to change:

- board wiring
- feature presence in the firmware build
- protocol or RAM limits
- default values used on first boot or after NVS is erased

After changing `Kconfig`:

1. rebuild the firmware
2. flash the new image
3. erase NVS as well if you want the new defaults to replace already persisted runtime values

The NVS behavior and persisted runtime keys are documented in [`runtime-config.md`](./runtime-config.md).
