# Receiving the 3 UWB distances on a tablet (BLE)

The MASTER (`receptec-qorvo`, BLE name `ATL-RANGE`) publishes one GATT
service. No pairing, no bonding, no app needed: any tablet browser with
Web Bluetooth (Chrome/Edge on Android) works. Open the page from
`https://` or `http://localhost` — `file://` has no Web Bluetooth.

## Connection

- Scan filter: name prefix `ATL`, or service UUID below.
- Connect, discover service `00003100-…`, subscribe to notifications on the
  three pair characteristics (and optionally metadata). Also readable
  directly at any time.

## GATT table (base `0000xxxx-4550-5453-4154-4c4554454300`)

| What | UUID short | Properties | Layout (all LE) |
|---|---|---|---|
| Range service | `0x3100` | — | — |
| Pair 0 = M–T | `0x3101` | read + notify | `status:u8, mm:u32` (5 B) |
| Pair 1 = A–T | `0x3102` | read + notify | same |
| Pair 2 = M–A | `0x3103` | read + notify | same |
| Metadata | `0x3104` | read + notify | `ver:u8(=1), pair:u8, source:u8, status:u8, round:u64, mm:u32, age_ms:u32` (20 B) |

Field notes:

- `status`: `0` = valid measurement. Anything else = no valid result;
  ignore `mm` in that case (it reads `0xFFFFFFFF`).
- `source`: `0` = measured on air, `1` = constant/fallback, `2` =
  unavailable. Only `0` feeds positioning.
- `round`: shared 64-bit round counter. A position needs all three pairs
  with the **same** round.
- `age_ms`: milliseconds since the firmware produced the sample
  (`0xFFFFFFFF` = unknown). Use it, not the arrival time, for staleness.
- Update rate follows the master cycle (500 ms default → ~2 tuples/s).

## Minimal Web Bluetooth sketch

```js
const base = '-4550-5453-4154-4c4554454300';
const uuid = s => '0000' + s.toString(16).padStart(4, '0') + base;
const dev = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: 'ATL' }],
  optionalServices: [uuid(0x3100)],
});
const server = await dev.gatt.connect();
const svc = await server.getPrimaryService(uuid(0x3100));
for (const pair of [0x3101, 0x3102, 0x3103]) {
  const chr = await svc.getCharacteristic(uuid(pair));
  const v = await chr.readValue();           // status = v.getUint8(0)
  await chr.startNotifications();            // mm = v.getUint32(1, true)
  chr.addEventListener('characteristicvaluechanged', e => {
    const d = e.target.value;
    onSample(pair, d.getUint8(0), d.getUint32(1, true));
  });
}
```

A complete working example with position computation is
`tools/range-dashboard.html` (+ `tools/uwb-position.js`).

## Native Android (Kotlin) pointers

- `BluetoothLeScanner` with a service-UUID filter on
  `00003100-4550-5453-4154-4c4554454300`, connect, discover, then
  `setCharacteristicNotification()` + write the CCCD (`0x2902`, value
  `0x0100`) for each of the three pair characteristics.
- Parse exactly as above; no pairing dialog appears (open peripheral).
- Keep the radio path short: one BLE connection at a time (firmware allows
  a single central).

## If nothing arrives

1. Confirm the master is ranging (`uwb range cycle status` shows
   `running=1` and climbing successes on all three pairs).
2. A pair that never completes publishes `status != 0` forever — that is a
   ranging problem, not a BLE problem.
3. Two boards may advertise `ATL-RANGE` (a stale anchor image does); check
   the device address matches the master before debugging the app.
