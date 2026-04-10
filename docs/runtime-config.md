# Runtime Configuration and NVS

## Overview

This document describes how runtime configuration is handled in the firmware and how selected values are persisted across reboots.
The MQTT command and reply topics remain documented in [`ethernet-mqtt.md`](./ethernet-mqtt.md). This document focuses on configuration storage and restore behavior.

## Two Configuration Layers

The firmware uses two layers of configuration:

| Layer | Source | Role |
| --- | --- | --- |
| Build-time defaults | `Kconfig` -> `APP_*` macros | define the default values compiled into the firmware |
| Persistent runtime overrides | NVS namespace `runtime_cfg` | override selected defaults across reboots |

The practical rule is:

- `Kconfig` provides the fallback values
- NVS provides the persisted overrides when they exist

That means a new firmware build can still boot with old persisted runtime values if those keys are already stored in NVS.

## Current Persisted Keys

The current runtime config namespace is:

- `runtime_cfg`

The following keys are persisted today:

| NVS key | Runtime field | Default source | Meaning |
| --- | --- | --- | --- |
| `hb_int_s` | `heartbeat_interval_s` | `APP_HEARTBEAT_INTERVAL_S` | active heartbeat period |
| `motion_en` | `motion_detection_enabled` | `APP_MOTION_DEFAULT_ENABLED` | runtime PIR enable flag |
| `motion_wu` | `motion_warmup_ms` | `APP_MOTION_DEFAULT_WARMUP_MS` | PIR warm-up duration |
| `motion_cd` | `motion_cooldown_ms` | `APP_MOTION_DEFAULT_COOLDOWN_MS` | PIR cooldown duration |

These are the only keys currently managed by `runtime_config.c`.

## Boot Restore Flow

The runtime restore flow is:

1. `main.c` calls `nvs_flash_init()`.
2. `main.c` calls `runtime_config_init()`.
3. `runtime_config_init()` seeds its in-memory state from the compiled defaults in `app_config.h`.
4. It opens the NVS namespace `runtime_cfg`.
5. For each known key, it tries to read a persisted value.
6. Missing keys are normal and simply mean "keep the compiled default".
7. Persisted values are validated before they overwrite the in-memory defaults.
8. Invalid persisted values are ignored and the compiled default is kept.
9. After init completes, `main.c` applies the restored heartbeat value before starting the heartbeat task.
10. `main.c` starts motion detection, which then reads the restored runtime motion settings.

Two extra notes matter here:

- if `nvs_flash_init()` reports `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, the app erases NVS and reinitializes it
- if PIR support is not compiled into the build, persisted `motion_detection_enabled=true` is ignored and runtime motion remains disabled

## MQTT `cmd/config` Update Flow

Runtime updates currently come from the MQTT `cmd/config` command.

The command router:

1. parses the supported JSON fields from the command payload
2. builds one `runtime_config_patch_t`
3. validates the full patch
4. persists the changed keys in NVS when requested
5. updates the in-memory runtime config state
6. applies the live changes to the running modules
7. replies with the active values

Example payload:

```json
{
  "request_id": "req-50",
  "heartbeat_interval_s": 10,
  "motion_detection_enabled": true,
  "motion_warmup_ms": 30000,
  "motion_cooldown_ms": 5000
}
```

Current rules:

- only supported keys are parsed
- unknown keys are ignored
- missing keys leave the current runtime value unchanged
- the command still requires a JSON `request_id` because replies are published on `reply/{request_id}`

## Validation and Atomic Persistence

The important guarantee today is:

- supported keys are validated together before the NVS write happens
- if validation fails, no runtime config key is committed to NVS

This avoids the earlier failure mode where one `cmd/config` could leave a half-updated persistent state.

Persistence is handled as one logical transaction:

- all changed keys are written into the opened `runtime_cfg` namespace
- one `nvs_commit()` is executed at the end

If the commit fails:

- the command reply is an error
- the new persistent state is not considered accepted

After a successful commit:

- the in-memory runtime state is updated
- the live modules are updated from that new state

This document uses "atomic" in the NVS sense: the persisted config patch is committed as one unit.

## Unsupported Features and Motion Settings

`motion_*` runtime keys only make sense when PIR support exists in the current build.

If `APP_HAS_MOTION_DETECTION` is disabled:

- `motion_detection_enabled`
- `motion_warmup_ms`
- `motion_cooldown_ms`

are rejected as `unsupported_feature` when sent through `cmd/config`.

This keeps the model simple:

- build-time support decides whether the PIR feature exists in this firmware
- runtime config only controls that feature when it is actually available

The PIR behavior itself is documented in [`motion-detection.md`](./motion-detection.md).

## Reboot, Rebuild, and Reflash Behavior

### Reboot

On a normal reboot:

- the NVS keys are still present
- the same runtime overrides are restored again

### New firmware build with new `Kconfig` defaults

If you flash a new firmware build with different defaults:

- the new `Kconfig` values become the new fallback defaults
- but existing NVS keys still take priority for the persisted fields

Example:

- old build default: `APP_HEARTBEAT_INTERVAL_S = 30`
- runtime override stored in NVS: `heartbeat_interval_s = 10`
- new build default: `APP_HEARTBEAT_INTERVAL_S = 60`

After reboot, the active value is still:

- `10`

because the persisted NVS override still exists.

### Erasing flash or NVS

If you erase flash, or erase the NVS partition before flashing:

- the persisted runtime overrides are removed
- the next boot uses only the defaults compiled into the new firmware

This is the current practical way to force a clean reset to new firmware defaults when reflashing.

## Current Reset Strategy

There is no dedicated MQTT "reset config" command yet.

Today, the supported ways to return to clean defaults are:

- erase the NVS partition
- or erase the whole flash before flashing the new firmware

After that:

- `runtime_config_init()` finds no stored overrides
- the firmware falls back entirely to the `APP_*` defaults compiled into the image

## Related Documents

- [`ethernet-mqtt.md`](./ethernet-mqtt.md)
- [`motion-detection.md`](./motion-detection.md)
