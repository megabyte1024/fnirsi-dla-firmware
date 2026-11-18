# Third-party code

Code here is not ours and keeps its own licence. Both libraries are git submodules, pinned to the commits below; run
`git submodule update --init` after cloning. Only the files the firmware compiles or includes are used.

One local change (Apache-2.0 section 4: modified files carry this notice here): the build copies the sources of
`wch-ch56x-lib` into its build folder and applies `patches/wch-ch56x-lib-usb30-hooks.patch` to the copy, so the submodule
stays unmodified. The patch changes `usb/usb30.c` and `usb/usb30.h`: two weak hooks, `usb30_take_ep_in(endp)` (an IN
completion the application handles itself; called before the library's own IN handling) and
`usb30_clear_endpoint_halt(address)` (called on CLEAR_FEATURE ENDPOINT_HALT to an endpoint, which the library left as a
TODO). Without an application override both do nothing, as before.

| Folder | Origin | Commit | Licence |
|---|---|---|---|
| `wch-ch56x-lib/` | https://github.com/hydrausb3/wch-ch56x-lib | `3b056b5246eba7e6a20af828d0284a59e3799883` (2025-02-24) | Apache-2.0 (`wch-ch56x-lib/LICENSE`); copyright Nanjing Qinheng Microelectronics (WCH), Benjamin Vernoux, Quarkslab |
| `wch-ch56x-bsp/` | https://github.com/hydrausb3/wch-ch56x-bsp | `eb6318d0c61c05baa527f59c47e5b967e4e9b00e` (2023-10-05) | Apache-2.0 (`wch-ch56x-bsp/LICENSE`); copyright Nanjing Qinheng Microelectronics (WCH), Benjamin Vernoux |

The firmware uses the USB 3 device stack of `wch-ch56x-lib` for its USB 3 link, and from `wch-ch56x-bsp` only the
register definitions (`drv/CH56xSFR.h`) and `rvmsis/core_riscv.h`. The support package sources of `wch-ch56x-bsp` are
not used.
