# Qorvo DW3XXX driver provenance

This directory contains the subset of Qorvo's DW3XXX device driver required for
the DW3000 family used by the DWM3001C/DW3110.

- Driver version: `08.19.02` (`DRIVER_VERSION_HEX 0x081902`)
- File-level license identifier: `LicenseRef-QORVO-2`
- Copyright: Qorvo US, Inc., 2024
- Internal source snapshot: `nrf52-bringup-fw` commit
  `628d5de9c54f35152f86a6249b66ef7ec655c3b6`
- Original internal path: `src/uwb/dwt_uwb_driver/`
- Official Qorvo download record: <https://www.qorvo.com/products/d/da008582>

The imported vendor files are byte-for-byte copies of that committed snapshot.
Only the common, DW3000-family, and qmath files were imported; DW3720 sources
are not needed by this DWM3001C application.

`LicenseRef-QORVO-2` is a proprietary Qorvo license, not an open-source SPDX
license. The source snapshot available in the workspace did not include the
corresponding license text. Use and redistribution of these files must comply
with the Qorvo agreement under which the official package was obtained. Obtain
the complete current license from the official package before distributing the
vendor sources outside the authorized project.

The checksums in `SHA256SUMS` cover the imported vendor files and exclude this
provenance document and the checksum file itself.
