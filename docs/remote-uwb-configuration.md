# Remote UWB configuration commands

Experimental commissioning extension. The protocol document was intentionally not edited at the operator request. Both endpoint firmware families must include this implementation; an older endpoint times out. Broadcast, PHY tuning and switching firmware roles are outside this version.

MASTER pauses its periodic ranging cycle, allows existing authorization to expire, performs a bounded request with up to three attempts, and restores the cycle. This maintenance pause is expected; configuration does not compete with an active DS-TWR exchange. Remote writes validate and persist settings with readback. Successful SET returns status 1 (saved, reboot required); GET reports saved values, not necessarily the active boot configuration. Status 0 means success; negative status is a remote errno. A transport timeout can mean an uncertain write outcome: query the value before retrying. Same-value writes are idempotent.

## Shell usage

Current MASTER peer slot 0 is BASE A and slot 1 is TAG. Slot numbers inside a target are its own peer indices, not the MASTER slot numbers.

```sh
uwb config get 0 role
uwb config get 1 role
uwb config get 0 identity
uwb config get 1 peer 0
uwb config set 0 peer 1 C2:3C:F4:90:45:6F:59:D2
uwb config set 1 peer 1 83:B0:09:B0:3C:04:B9:BF
uwb config clear 0 peer 2
uwb config reboot 0
uwb config reboot 1
```

Supported keys: identity and peer on BASE/TAG; enabled, auto and cycle on RECEPTEC targets; role is read-only (MASTER=0, BASE=1, TAG=2). GET peer returns unset for an empty slot. Clear uses the peer key and cannot remove controller slot 0. BASE auto off and disabling its controller are rejected to preserve remote management. To omit a node from ranging, use MASTER's local peer disable/enable and cycle controls; management may still address a registered disabled node.

A cycle write uses `uwb config set <target> cycle <tag-slot> <base-slot> <250..1000 period-ms> <autostart on|off> <baseline-mm;0=measured>`. A fixed BASE rejects autostart on: it cannot become a second MASTER. TAG has no independent agenda and rejects cycle/enabled/auto. The active common sampling period belongs to MASTER; change it with the existing local cycle period command while stopped. Remote BASE cycle settings are inactive unless an appropriate controller image is installed later.

## EUI migration

Saving a new endpoint identity does not immediately change its address. Before reboot, update the reciprocal peer on the other endpoint (BASE's TAG peer or TAG's BASE peer), then query all saved values. Confirmed remote reboot reads the staged identity and remaps MASTER's own peer slot, retaining nickname/enabled state. It does not rewrite other nodes automatically. A lost reboot reply is uncertain: the endpoint may already have switched. Check old/new addresses and restore MASTER's local peer mapping before sending further writes. Changing controller slot 0 requires the designated new MASTER to be provisioned; the old controller cannot manage the target after that reboot.

Reboot acknowledgment means the restart was accepted, not that boot completed. Confirm with a subsequent GET and a ranging capture. The existing shell remains the recovery interface. Requests are accepted only from the boot-cached commissioned MASTER EUI and the active destination EUI; this is address admission, not cryptographic authentication.
