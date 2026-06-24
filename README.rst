RECEPTEC DWM3001C
#################

This application implements the RECEPTEC side of the RECEPTEC–COLETEC UWB
protocol on the Qorvo DWM3001C.

Current status
**************

Implemented:

* byte-exact protocol frame encoder and decoder;
* opaque DATA payloads up to 105 bytes;
* DATA_REQ, SYNC, DATA, ACK, STOP, and STOP_ACK types;
* 16-bit little-endian ``DT_sampling`` encoding;
* initial timing profile and configuration validation;
* host-side protocol tests;
* successful Zephyr build for ``decawave_dwm3001cdk``.

Pending:

* import of an explicitly versioned Qorvo ``dwt_uwb_driver`` release;
* Zephyr SPI/GPIO/IRQ adapter for the DW3110;
* hardware TX/RX and deterministic slot scheduler;
* hardware interoperability testing with COLETEC.

The shared implementation contract is in the ``contracts`` submodule at
``contracts/protocol/uwb-receptec-coletec/HANDOFF.md``.

Build
*****

.. code-block:: console

   west build -b decawave_dwm3001cdk -d build-zephyr .

Run portable protocol tests
***************************

.. code-block:: console

   cmake -S tests -B build-host
   cmake --build build-host
   ctest --test-dir build-host --output-on-failure

UWB driver dependency
*********************

Upstream Zephyr supports the DWM3001CDK's nRF52833 board resources but does
not currently provide the DW3110 UWB driver required by this application. The
board's existing ``zephyr,ieee802154`` choice is the Nordic 2.4 GHz radio and
is not used by this protocol.
