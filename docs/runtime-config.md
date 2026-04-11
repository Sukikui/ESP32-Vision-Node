# Runtime Configuration and NVS

## Overview

This document describes the runtime configuration layer implemented in `components/runtime_config/`.

The MQTT command and reply topics remain documented in [`ethernet-mqtt.md`](./ethernet-mqtt.md). The `Kconfig` defaults that feed this runtime layer remain documented in [`kconfig.md`](./kconfig.md).

## Configuration Sources

| Source | Scope | Role |
| --- | --- | --- |
| `Kconfig` / `APP_*` macros | build time | define the defaults compiled into the firmware |
| NVS namespace `runtime_cfg` | runtime persistence | override selected defaults across reboots |
| `runtime_config` in-memory state | live firmware state | provide the active values used by running modules |

The effective rule is:

- if no persisted value exists, the firmware uses the compiled default
- if a persisted value exists, it overrides the compiled default after reboot

## Persisted Keys

| Runtime field | JSON key | NVS key | Default source | Feature gate | Live consumer |
| --- | --- | --- | --- | --- | --- |
| `heartbeat_interval_s` | `heartbeat_interval_s` | `hb_int_s` | `APP_HEARTBEAT_INTERVAL_S` | none | `heartbeat_task` |
| `motion_detection_enabled` | `motion_detection_enabled` | `motion_en` | `APP_MOTION_DEFAULT_ENABLED` | `APP_HAS_MOTION_DETECTION` | `motion_detection` |
| `motion_warmup_ms` | `motion_warmup_ms` | `motion_wu` | `APP_MOTION_DEFAULT_WARMUP_MS` | `APP_HAS_MOTION_DETECTION` | `motion_detection` |
| `motion_cooldown_ms` | `motion_cooldown_ms` | `motion_cd` | `APP_MOTION_DEFAULT_COOLDOWN_MS` | `APP_HAS_MOTION_DETECTION` | `motion_detection` |
| `ir_illuminator_mode` | `ir_illuminator_mode` | `ir_mode` | `APP_IR_ILLUMINATOR_DEFAULT_MODE` | `APP_HAS_IR_ILLUMINATOR` | `ir_illuminator` |

These are the only keys currently managed by `runtime_config.c`.

## Boot Restore Sequence

| Step | Action |
| --- | --- |
| 1 | `main.c` initializes NVS with `nvs_flash_init()` |
| 2 | `main.c` calls `runtime_config_init()` |
| 3 | `runtime_config_init()` seeds its state from the compiled `APP_*` defaults |
| 4 | `runtime_config_init()` opens the `runtime_cfg` namespace |
| 5 | each known key is read from NVS when present |
| 6 | each restored value is validated before it overwrites the default |
| 7 | invalid persisted values are ignored and the compiled default is kept |
| 8 | `main.c` applies the restored IR mode before network startup |
| 9 | `main.c` applies the restored heartbeat interval before starting the heartbeat task |
| 10 | `motion_detection` later reads the restored motion settings when it starts |

Additional boot rules:

| Condition | Behavior |
| --- | --- |
| `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` during NVS init | erase NVS and initialize it again |
| PIR support not compiled in | persisted `motion_*` values are not allowed to enable live PIR behavior |
| IR illuminator support not compiled in | persisted non-`off` IR modes are ignored at runtime |

## MQTT Update Path

Runtime updates currently come from `vision/nodes/{node_id}/cmd/config` and `vision/broadcast/cmd/config`.

| Step | Action |
| --- | --- |
| 1 | `command_router.c` parses supported JSON keys into one `runtime_config_patch_t` |
| 2 | the full patch is validated |
| 3 | valid changed keys are persisted in one NVS transaction |
| 4 | the in-memory runtime state is updated |
| 5 | live modules re-apply the resulting values |
| 6 | the reply payload returns the active values |

## Validation Rules

| Field | Accepted values |
| --- | --- |
| `heartbeat_interval_s` | `1..3600` |
| `motion_warmup_ms` | `0..120000` |
| `motion_cooldown_ms` | `0..60000` |
| `ir_illuminator_mode` | `off`, `on`, `capture` |

Common request rules:

| Rule | Behavior |
| --- | --- |
| missing supported key | leave the current value unchanged |
| unknown key | ignore it |
| missing `request_id` | reject the command because the reply topic cannot be built |
| invalid supported value | reject the whole update |
| unsupported feature key for this build | reject the whole update as `unsupported_feature` |

## Atomic Persistence

`cmd/config` persistence is all-or-nothing at the runtime-config level.

| Case | Result |
| --- | --- |
| full patch is valid and `nvs_commit()` succeeds | all changed keys are accepted and become the new persisted state |
| one requested value is invalid | no key from that patch is committed |
| `nvs_commit()` fails | no new persisted state is accepted |

This prevents partial persistent updates from one failing config command.

## Feature Gates

| Build-time gate | Protected runtime fields |
| --- | --- |
| `APP_HAS_MOTION_DETECTION` | `motion_detection_enabled`, `motion_warmup_ms`, `motion_cooldown_ms` |
| `APP_HAS_IR_ILLUMINATOR` | `ir_illuminator_mode` |

If the feature gate is disabled in the current firmware build, the corresponding runtime keys are rejected when received through `cmd/config`.

## Reboot and Reflash Behavior

| Situation | Result |
| --- | --- |
| normal reboot | persisted runtime values are restored again |
| new firmware with different `Kconfig` defaults, but existing NVS keys | persisted runtime values still win |
| erase NVS before flashing | runtime overrides disappear and the firmware falls back to the compiled defaults |
| erase full flash before flashing | same result for runtime config, plus full device reset |

Example:

| Item | Value |
| --- | --- |
| old compiled default | `APP_HEARTBEAT_INTERVAL_S = 30` |
| persisted runtime value | `heartbeat_interval_s = 10` |
| new compiled default | `APP_HEARTBEAT_INTERVAL_S = 60` |
| active value after reboot | `10` |

## Current Reset Strategy

There is no dedicated MQTT reset command yet.

Current ways to return to the compiled defaults:

| Action | Result |
| --- | --- |
| erase the NVS partition | remove persisted runtime overrides only |
| erase the whole flash | remove persisted runtime overrides and reset the whole device state |

## Related Documents

- [`ethernet-mqtt.md`](./ethernet-mqtt.md)
- [`kconfig.md`](./kconfig.md)
- [`motion-detection.md`](./motion-detection.md)
- [`ir-illuminator.md`](./ir-illuminator.md)
