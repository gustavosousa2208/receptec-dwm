RECEPTEC DWM3001C
#################

This application is the hardware bring-up foundation for the RECEPTEC–COLETEC
UWB protocol on the Qorvo DWM3001C.

Current status
**************

Current milestone:

* retrieve the nRF52833 factory device ID through Zephyr's hardware-info API;
* print the complete eight-byte ID in a stable hexadecimal format;
* assign RECEPTEC to device ID ``3E:C6:F3:82:06:A8:16:41``;
* assign every other device ID to COLETEC;
* compile and link the pinned Qorvo DW3XXX 08.19.02 driver;
* initialize the application-owned SPI/reset/IRQ adapter and matched PHY.

Pending:

* verify the DW3110 probe and initialization diagnostics on both boards;
* hardware TX/RX and the deterministic slot scheduler;
* hardware interoperability testing with COLETEC.

The shared implementation contract is in the ``contracts`` submodule at
``contracts/protocol/uwb-receptec-coletec/HANDOFF.md``.

Build
*****

.. code-block:: console

   west build -b decawave_dwm3001cdk .

Flash both experimental boards
******************************

Build once and flash both configured J-Link probes concurrently:

.. code-block:: console

   ./scripts/flash-both.sh

UWB driver dependency
*********************

Upstream Zephyr supports the DWM3001CDK's nRF52833 board resources but does
not currently provide the DW3110 UWB driver required by this application. The
board's existing ``zephyr,ieee802154`` choice is the Nordic 2.4 GHz radio and
is not used by this protocol. This application pins Qorvo DW3XXX driver
08.19.02 under ``vendor/qorvo``; see its README for provenance and proprietary
license information.
