# BLE sampling control

Open tools/range-dashboard.html over HTTPS and connect to the MASTER. Sampling controls require the new 0x3105 characteristic; older firmware remains usable for distances.

- Apply temporarily: 250–1000 ms, all three pairs at the same rate; takes effect before the next round, without resetting round IDs or counters. No flash write.
- Save active period: persist the complete current cycle configuration and verify the saved period. Do this only after evaluating results.
- Restore 500 ms: temporary return to the operational reference, not an implicit flash write. Save separately if it should survive boot.
- Read state: active/saved periods, pending request, counters per pair and overruns. Success ratios are cumulative; compare counter deltas for the new period.

Presets: 500 ms/2 Hz reference; 300 ms/3.33 Hz previously qualified for 15 minutes on the earlier bench; 250 ms/4 Hz experimental (long capture had host cadence violations); 1000 ms/1 Hz slower. These historical results do not qualify a changed physical arrangement. Invalid input is rejected without altering the rate. One request may be pending; concurrent requests are rejected. Disconnect does not cancel an accepted request, and reconnection can read its result. Temporary settings revert to the saved configuration on reboot. There is no automatic quality-based rollback.

The extension is bounded rate control, not a shell or OTA. BASE images reject writes. Existing BLE access permissions allow a connected central to control it; it has no additional authentication in this version.

## Local GATT integration

Service UUID remains 00003100-4550-5453-4154-4c4554454300. Characteristic 00003105-4550-5453-4154-4c4554454300 supports READ and WRITE with response. Write exactly 8 bytes: version1 u8, op u8 (1=temporary,2=save active,3=restore500), request ID u16 LE, period u32 LE. Ops2/3 require period0. Prepared/offset writes are rejected. A write response accepts the request; poll state for completion with the same ID.

Read48 bytes: version/running/pending/configured at0..3; active period u32 at4; saved period u32 at8; request ID u16 at12; signed status i16 at14 (1=pending,0=applied,negative=error); success/failure u32 pairs at16/20,24/28,32/36; overruns u32 at40; pending period u32 at44 (zero for SAVE).

Flash saving runs between exchanges, not in the GATT RX callback. It can extend one interval; avoid saving during benchmark captures. The BLE callback only validates and queues a bounded request. UWB remains the owner of the sampling schedule.
