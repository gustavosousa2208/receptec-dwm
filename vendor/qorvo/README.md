# Qorvo DW3XXX driver provenance

This directory contains the Qorvo DW3XXX driver subset used by the DWM3001C
(DW3110) and RECEPTEC Type2AB (QM33120W/DW3720) applications.

- Driver version: `08.19.02` (`DRIVER_VERSION_HEX 0x081902`)
- File-level license identifier: `LicenseRef-QORVO-2`
- Copyright: Qorvo US, Inc., 2024
- Internal source snapshot: `nrf52-bringup-fw` commit
  `628d5de9c54f35152f86a6249b66ef7ec655c3b6`
- Original internal path: `src/uwb/dwt_uwb_driver/`
- Official Qorvo download record: <https://www.qorvo.com/products/d/da008582>

The imported vendor files are byte-for-byte copies of that committed snapshot.
The common, DW3000-family, DW3720-family, and qmath files were imported from
that snapshot. The DW3720-family files are required by the RECEPTEC app.

`LicenseRef-QORVO-2` is a proprietary Qorvo license, not an open-source SPDX
license. The license text supplied with the official archive is retained at
`LICENSES/LicenseRef-QORVO-2.txt`. Use and redistribution must comply with its
terms; do not distribute outside the authorized project without confirming
the applicable grant.

The checksums in `SHA256SUMS` cover the imported vendor files and exclude this
provenance document and the checksum file itself.
