# Zephyr patches

Place reproducible Zephyr source patches in this directory using lexically
ordered names such as:

```text
0001-dw3110-add-devicetree-binding.patch
0002-dw3110-add-board-overlay-support.patch
```

Patches are applied in filename order by
`../../scripts/apply-zephyr-patches.sh`. Keep application-owned code outside
the Zephyr tree whenever possible.

There are currently no RECEPTEC Zephyr patches.
