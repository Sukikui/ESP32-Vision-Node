# ESP32-P4 Ethernet + MQTT Architecture

## Overview

This document describes the target network control-plane and MQTT transport model for the Waveshare `ESP32-P4-ETH` board.

Scope:

- wired Ethernet over the internal ESP32-P4 EMAC
- DHCP IPv4 addressing
- MQTT client connection to a broker running on the Raspberry Pi
- retained online/offline presence
- periodic heartbeat publication
- incoming MQTT command handling
- application events over MQTT
- RAM-backed publish queue with retry
- image transfer over MQTT using metadata, binary chunks, and a final completion marker

All application-level traffic is expected to go through MQTT. No HTTP transport is assumed in this architecture.

## OSI Network Stack

| Layer | Choice |
| --- | --- |
| Physical / link | Ethernet |
| Network | IPv4 via DHCP |
| Transport | TCP |
| Application | MQTT |
| Payload | JSON for control/status/events, binary chunks for images |

## System Topology

| Component | Role |
| --- | --- |
| ESP32-P4 node | Ethernet-connected edge node |
| Ethernet PHY | `IP101GRI` over `RMII` |
| Raspberry Pi 4/5 | Central unit |
| MQTT broker on RPi | Message hub, typically Mosquitto |
| RPi consumer service | Subscribes to node status, replies, events, and image topics |

## Hardware Configuration

The firmware is configured for the Waveshare `ESP32-P4-ETH` board with the following mapping:

| Ethernet function | ESP32-P4 GPIO |
| --- | --- |
| TX_EN | `GPIO49` |
| TXD0 | `GPIO34` |
| TXD1 | `GPIO35` |
| CRS_DV | `GPIO28` |
| RXD0 | `GPIO29` |
| RXD1 | `GPIO30` |
| REF_CLK input | `GPIO50` |
| MDC | `GPIO31` |
| MDIO | `GPIO52` |
| PHY reset | `GPIO51` |
| PHY address | `1` |

Notes:

- the firmware uses the internal ESP32-P4 EMAC
- the MAC/PHY interface is `RMII`
- the RMII reference clock is configured as external clock input
- no manual `gpio_config()` is used for the standard RMII data lines

## Project Configuration

The project exposes these build-time and default runtime configuration values through `menuconfig`:

| Key | Meaning |
| --- | --- |
| `APP_NODE_ID` | MQTT node identifier |
| `APP_HEARTBEAT_INTERVAL_S` | default heartbeat period in seconds |
| `APP_MQTT_BROKER_PORT` | TCP port used for the MQTT broker |
| `APP_TOPIC_MAX_LEN` | maximum topic buffer length |
| `APP_JSON_PAYLOAD_MAX_LEN` | maximum length for small JSON payload buffers |
| `APP_REQUEST_ID_MAX_LEN` | maximum request ID length |
| `APP_PUBLISH_QUEUE_LENGTH` | maximum number of queued outgoing MQTT messages |
| `APP_PUBLISH_MAX_DATA_LEN` | maximum payload size stored in one queued MQTT message |
| `APP_IMAGE_CHUNK_SIZE` | maximum image bytes sent in one MQTT chunk |
| `APP_HAS_MOTION_DETECTION` | build-time gate that includes PIR support in this firmware |
| `APP_MOTION_PIR_GPIO` | GPIO connected to the PIR digital output |
| `APP_MOTION_PIR_ACTIVE_HIGH` | trigger on rising edge when enabled |
| `APP_MOTION_DEFAULT_ENABLED` | default runtime enabled state used before any NVS override exists |
| `APP_MOTION_DEFAULT_WARMUP_MS` | default warm-up period used before any NVS override exists |
| `APP_MOTION_DEFAULT_COOLDOWN_MS` | default cooldown period used before any NVS override exists |

MQTT broker discovery follows this convention:

- the Raspberry Pi runs the DHCP server for the isolated Ethernet network
- the Raspberry Pi also hosts the MQTT broker
- the ESP32 uses the DHCP default gateway IP as the central unit IP
- the MQTT broker URI is built automatically as `mqtt://<gateway_ip>:<APP_MQTT_BROKER_PORT>`

External component dependencies are declared in the component manifests:

- `components/connectivity/idf_component.yml` for `espressif/ip101`
- `components/messaging/idf_component.yml` for `espressif/mqtt`

The resolved versions are stored in `dependencies.lock`.

## Firmware Modules

| File | Responsibility |
| --- | --- |
| `main/main.c` | boot sequence and service startup |
| `components/connectivity/ethernet_service.c` | Ethernet driver, netif attachment, link/IP state |
| `components/detection/motion_detection.c` | PIR GPIO interrupt, warm-up, cooldown, and motion event emission |
| `components/runtime_config/runtime_config.c` | persisted runtime settings restored from NVS |
| `components/messaging/mqtt_service.c` | MQTT client lifecycle and publish API |
| `components/messaging/topic_map.c` | MQTT topic construction helpers |
| `components/messaging/command_router.c` | incoming command parsing and dispatch |
| `components/messaging/node_event.c` | event publication helper |
| `components/messaging/heartbeat_task.c` | periodic heartbeat publication |
| `components/messaging/publish_queue.c` | RAM-backed outgoing publish queue with retry |
| `components/messaging/image_transfer.c` | helper to publish image metadata, chunks, and done marker |
| `components/app_config/include/app_config.h` | compile-time application constants |

## Boot Flow

The expected boot order is:

1. initialize `nvs_flash`
2. load persisted runtime overrides from NVS
3. initialize `esp_netif`
4. create the default event loop
5. register the motion detection event handler if PIR support is compiled into this build
6. initialize topic mapping from `APP_NODE_ID`
7. initialize Ethernet service
8. initialize and start the publish queue
9. initialize PIR motion detection
10. start Ethernet
11. wait for DHCP IPv4 address
12. read the DHCP gateway and derive the MQTT broker URI
13. initialize MQTT
14. start MQTT
15. apply the restored heartbeat interval to the heartbeat task
16. start PIR motion detection
17. start the heartbeat task
18. publish `boot_completed`

## MQTT Topic Catalog

In the `Sent by` column, `Broker` means a publisher connected from the Raspberry Pi side of the system. The only message actually emitted by the broker process itself is the `offline` Last Will on the `status/online` topic.

| Topic | Sent by | QoS | Retain | Purpose |
| --- | --- | --- | --- | --- |
| `vision/nodes/{node_id}/status/online` | `ESP / Broker` | `1` | `true` | Presence state for one node |
| `vision/nodes/{node_id}/status/heartbeat` | `ESP` | `0` | `false` | Periodic node status |
| `vision/nodes/{node_id}/event` | `ESP` | `1` | `false` | One-off node events |
| `vision/nodes/{node_id}/cmd/ping` | `Broker` | `1` | `false` | Targeted ping command |
| `vision/broadcast/cmd/ping` | `Broker` | `1` | `false` | Broadcast ping command |
| `vision/nodes/{node_id}/cmd/config` | `Broker` | `1` | `false` | Targeted runtime configuration command |
| `vision/broadcast/cmd/config` | `Broker` | `1` | `false` | Broadcast runtime configuration command |
| `vision/nodes/{node_id}/cmd/reboot` | `Broker` | `1` | `false` | Targeted reboot command |
| `vision/broadcast/cmd/reboot` | `Broker` | `1` | `false` | Broadcast reboot command |
| `vision/nodes/{node_id}/cmd/capture` | `Broker` | `1` | `false` | Targeted image capture command |
| `vision/broadcast/cmd/capture` | `Broker` | `1` | `false` | Broadcast image capture command |
| `vision/nodes/{node_id}/reply/{request_id}` | `ESP` | `1` | `false` | Primary command reply channel |
| `vision/nodes/{node_id}/image/{capture_id}/meta` | `ESP` | `1` | `false` | Image transfer metadata |
| `vision/nodes/{node_id}/image/{capture_id}/chunk/{index}` | `ESP` | `0` | `false` | Image binary chunk |
| `vision/nodes/{node_id}/image/{capture_id}/done` | `ESP` | `1` | `false` | End-of-image marker |

## Topic Details and Examples

### 🔌 `vision/nodes/{node_id}/status/online`

Purpose:

- indicate whether a node is currently considered online by the MQTT system
- keep the latest known presence state available to late subscribers because `retain=true`

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Topic example</td>
<td><code>vision/nodes/p4-001/status/online</code></td>
</tr>
<tr>
<td>Payload when the node connects</td>
<td>

```json
{
  "state": "online",
  "node_id": "p4-001"
}
```

</td>
</tr>
<tr>
<td>Payload published by the broker via Last Will</td>
<td>

```json
{
  "state": "offline",
  "node_id": "p4-001"
}
```

</td>
</tr>
</table>

### 🔌 `vision/nodes/{node_id}/status/heartbeat`

Purpose:

- publish periodic health and network state
- expose fresh runtime information even when the node stays online for a long time
- complement `status/online`; it is not itself the loss-of-connection mechanism

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Topic example</td>
<td><code>vision/nodes/p4-001/status/heartbeat</code></td>
</tr>
<tr>
<td>Payload example</td>
<td>

```json
{
  "node_id": "p4-001",
  "ip": "192.168.50.20",
  "uptime_s": 1234
}
```

</td>
</tr>
</table>

### 🔌 `vision/nodes/{node_id}/event`

Purpose:

- publish one-off facts rather than periodic state
- notify the Raspberry Pi about noteworthy node-side activity

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Topic example</td>
<td><code>vision/nodes/p4-001/event</code></td>
</tr>
<tr>
<td>Generic payload shape</td>
<td>

```json
{
  "node_id": "p4-001",
  "event": "boot_completed",
  "timestamp_ms": 123456789
}
```

</td>
</tr>
</table>

Possible event values:

- `boot_completed`
- `config_updated`
- `capture_failed`
- `motion_detected`

### 🔌 `vision/nodes/{node_id}/cmd/*` and `vision/broadcast/cmd/*`

Purpose:

- receive commands from the Raspberry Pi side
- distinguish between a command for one node and a command for every listening node

Node-specific and broadcast commands use the same payload shape. The only difference is whether one node or all listening nodes should react.

For `config`, supported runtime values are validated together, committed to NVS in one transaction, and then applied live. If one requested value is invalid, the previous persisted config is left unchanged.

The `motion_*` runtime keys are only accepted when PIR support is compiled into the firmware via `APP_HAS_MOTION_DETECTION`.

Currently persisted runtime config keys are:

- `heartbeat_interval_s`
- `motion_detection_enabled`
- `motion_warmup_ms`
- `motion_cooldown_ms`

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Node-specific topic examples</td>
<td>
<code>vision/nodes/p4-001/cmd/ping</code><br>
<code>vision/nodes/p4-001/cmd/config</code><br>
<code>vision/nodes/p4-001/cmd/reboot</code><br>
<code>vision/nodes/p4-001/cmd/capture</code>
</td>
</tr>
<tr>
<td>Broadcast topic examples</td>
<td>
<code>vision/broadcast/cmd/ping</code><br>
<code>vision/broadcast/cmd/config</code><br>
<code>vision/broadcast/cmd/reboot</code><br>
<code>vision/broadcast/cmd/capture</code>
</td>
</tr>
<tr>
<td>Example payload for <code>ping</code></td>
<td>

```json
{
  "request_id": "req-42"
}
```

</td>
</tr>
<tr>
<td>Example payload for <code>config</code></td>
<td>

```json
{
  "request_id": "req-43",
  "heartbeat_interval_s": 30,
  "motion_detection_enabled": true,
  "motion_warmup_ms": 30000,
  "motion_cooldown_ms": 5000
}
```

</td>
</tr>
<tr>
<td>Example payload for <code>reboot</code></td>
<td>

```json
{
  "request_id": "req-44"
}
```

</td>
</tr>
<tr>
<td>Example payload for <code>capture</code></td>
<td>

```json
{
  "request_id": "req-45"
}
```

</td>
</tr>
</table>

A `capture` command is expected to trigger an image publication sequence on:

- `vision/nodes/{node_id}/image/{capture_id}/meta`
- `vision/nodes/{node_id}/image/{capture_id}/chunk/{index}`
- `vision/nodes/{node_id}/image/{capture_id}/done`

### 🔌 `vision/nodes/{node_id}/reply/{request_id}`

Purpose:

- provide the actual response channel for command execution
- allow the Raspberry Pi to correlate a command with a node-specific reply

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Reply topic example</td>
<td><code>vision/nodes/p4-001/reply/req-42</code></td>
</tr>
<tr>
<td>Reply payload for a ping</td>
<td>

```json
{
  "node_id": "p4-001",
  "ok": true,
  "ip": "192.168.50.20",
  "uptime_s": 123
}
```

</td>
</tr>
<tr>
<td>Reply payload for a config update</td>
<td>

```json
{
  "node_id": "p4-001",
  "ok": true,
  "heartbeat_interval_s": 30,
  "motion_detection_enabled": true,
  "motion_warmup_ms": 30000,
  "motion_cooldown_ms": 5000,
  "updated": true
}
```

</td>
</tr>
<tr>
<td>Reply payload for a reboot</td>
<td>

```json
{
  "node_id": "p4-001",
  "ok": true,
  "rebooting": true
}
```

</td>
</tr>
</table>


### 🔌 `vision/nodes/{node_id}/image/{capture_id}/meta`

Purpose:

- start an image transfer
- describe the image before binary data starts flowing

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Topic example</td>
<td><code>vision/nodes/p4-001/image/cap-001/meta</code></td>
</tr>
<tr>
<td>Payload example</td>
<td>

```json
{
  "capture_id": "cap-001",
  "content_type": "image/jpeg",
  "total_size": 48123,
  "chunk_size": 2048,
  "chunk_count": 24
}
```

</td>
</tr>
</table>

### 🔌 `vision/nodes/{node_id}/image/{capture_id}/chunk/{index}`

Purpose:

- carry the raw image data
- split one image into manageable MQTT-sized pieces

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Topic examples</td>
<td>
<code>vision/nodes/p4-001/image/cap-001/chunk/0</code><br>
<code>vision/nodes/p4-001/image/cap-001/chunk/1</code><br>
<code>vision/nodes/p4-001/image/cap-001/chunk/2</code>
</td>
</tr>
<tr>
<td>Payload example</td>
<td>

```text
<binary JPEG bytes>
```

</td>
</tr>
</table>

### 🔌 `vision/nodes/{node_id}/image/{capture_id}/done`

Purpose:

- mark the end of the image transfer for one `capture_id`
- tell the Raspberry Pi that all chunks for that transfer have been published

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Topic example</td>
<td><code>vision/nodes/p4-001/image/cap-001/done</code></td>
</tr>
<tr>
<td>Payload example</td>
<td>

```json
{
  "capture_id": "cap-001",
  "chunk_count": 24,
  "ok": true
}
```

</td>
</tr>
</table>

The Raspberry Pi side must reassemble the image by `capture_id`.

## Flow Examples

### Targeted Ping

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Command topic</td>
<td><code>vision/nodes/p4-001/cmd/ping</code></td>
</tr>
<tr>
<td>Command payload</td>
<td>

```json
{
  "request_id": "req-42"
}
```

</td>
</tr>
<tr>
<td>Reply topic</td>
<td><code>vision/nodes/p4-001/reply/req-42</code></td>
</tr>
<tr>
<td>Reply payload</td>
<td>

```json
{
  "node_id": "p4-001",
  "ok": true,
  "ip": "192.168.50.20",
  "uptime_s": 123
}
```

</td>
</tr>
</table>

### Broadcast Ping

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Command topic</td>
<td><code>vision/broadcast/cmd/ping</code></td>
</tr>
<tr>
<td>Command payload</td>
<td>

```json
{
  "request_id": "req-99"
}
```

</td>
</tr>
<tr>
<td>Reply topic from one node</td>
<td><code>vision/nodes/p4-001/reply/req-99</code></td>
</tr>
<tr>
<td>Reply payload from one node</td>
<td>

```json
{
  "node_id": "p4-001",
  "ok": true,
  "ip": "192.168.50.20",
  "uptime_s": 123
}
```

</td>
</tr>
<tr>
<td>Reply topic from another node</td>
<td><code>vision/nodes/p4-002/reply/req-99</code></td>
</tr>
<tr>
<td>Reply payload from another node</td>
<td>

```json
{
  "node_id": "p4-002",
  "ok": true,
  "ip": "192.168.50.21",
  "uptime_s": 456
}
```

</td>
</tr>
</table>

### Image Transfer

<table>
<tr>
<th>Item</th>
<th>Value</th>
</tr>
<tr>
<td>Metadata topic</td>
<td><code>vision/nodes/p4-001/image/cap-001/meta</code></td>
</tr>
<tr>
<td>Metadata payload</td>
<td>

```json
{
  "capture_id": "cap-001",
  "content_type": "image/jpeg",
  "total_size": 48123,
  "chunk_size": 2048,
  "chunk_count": 24
}
```

</td>
</tr>
<tr>
<td>Chunk topic</td>
<td><code>vision/nodes/p4-001/image/cap-001/chunk/0</code></td>
</tr>
<tr>
<td>Chunk payload</td>
<td>

```text
<binary JPEG bytes>
```

</td>
</tr>
<tr>
<td>Done topic</td>
<td><code>vision/nodes/p4-001/image/cap-001/done</code></td>
</tr>
<tr>
<td>Done payload</td>
<td>

```json
{
  "capture_id": "cap-001",
  "chunk_count": 24,
  "ok": true
}
```

</td>
</tr>
</table>

## Publish Queue

Outgoing application messages do not publish directly to MQTT by default.

Behavior:

- status, events, replies, and image messages are pushed into a RAM queue
- a dedicated task drains the queue
- if MQTT is disconnected, the queue task keeps retrying until publish succeeds
- the queue is not persistent across reboot
- if the queue is full, enqueue returns an error

Queue characteristics:

| Setting | Value |
| --- | --- |
| Queue length | `12` messages |
| Max payload stored per queue item | `2048` bytes |
| Retry delay | `250 ms` |

## Raspberry Pi Side

The firmware assumes the Raspberry Pi provides:

- an Ethernet network with DHCP
- an MQTT broker listening on port `1883`
- a DHCP lease where the default gateway IP is the Raspberry Pi IP

In practice:

- DHCP server = Raspberry Pi
- default gateway = Raspberry Pi
- MQTT broker host = Raspberry Pi

RPi should subscribe to:

- `vision/nodes/+/status/#`
- `vision/nodes/+/event`
- `vision/nodes/+/reply/+`
- `vision/nodes/+/image/+/#`
