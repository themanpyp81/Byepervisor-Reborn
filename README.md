# Byepervisor Reborn
**Note: This repo should be considered a base exploit for work to be built on top of. It is not useful to regular users in its current release state, this is intended for developers.**

## Summary
PS5 hypervisor exploit for 6.xx - 13.60 firmware. Bug details can be found [here](https://github.com/soniciso1/ps5-hv-writeups/blob/main/s3-resume-startup_mp-ap-launch.md). This bug was initially found by [theflow0](https://x.com/theflow0) and subsequently more people recently w/ assistance of LLMs. The TLDR is that on S3 resume, the `startup_mp` hypercall is invoked on core 0 to initialize the other AP cores (1-15), and takes a reset vector (`rip`) as one of the arguments for where the core will execute from. During this time on cores 1-15, an insecure Nested Page Table (NPT) config is applied. We can abuse this to run some shellcode that has R/W into the hypervisor's address space including the VM Control Blocks (VMCBs).

Very similarly to (one version of) [byepervisor of old](https://github.com/PS5Dev/Byepervisor), this exploits the rest mode path too :)

The following firmwares have offset tables in `include/offsets/`:

| Firmware | Table | Status |
|---|---|---|
| 12.00 | `12_00.h` | untested |
| 12.02 | `12_02.h` | untested |
| 12.20 | `12_20.h` | untested |
| 12.40 | `12_40.h` | untested |
| 12.60 | `12_60.h` | untested |
| 12.70 | `12_70.h` | untested |
| 13.00 | `13_00.h` | untested |
| 13.20 | `13_20.h` | tested |
| 13.40 | `13_40.h` | untested |
| 13.42 | `13_42.h` | untested |
| 13.60 | `13_60.h` | tested |

"untested" means the offset table is present and the payload builds and
dispatches for that version, but the exploit chain has not been confirmed on
hardware for it. Only report a firmware as working once you have run it.

Some firmwares share an identical kernel and therefore share an identical
offset table: 12.00/12.02, 12.20/12.40, and 12.60/12.70. The tables are
byte-identical within each of those pairs.

However this bug should technically be exploitable as low as 6.xx. With modifications, it could work on 14.00 as well, but this is not provided here, especially since there is no public 14.00 kernel exploit anyways.

## Important Notes
- You may need to go to Settings -> System -> Power Saving -> Features Available in Rest Mode
  - Set Supply Power to USB Ports to "Always"
- This payload is designed to run through the [elfldr](https://github.com/ps5-payload-dev/elfldr)
  - This is somewhat important because the payload expects to continue running on resume from rest mode, and it relies on elfldr for this
- Post-resume, this payload should leave the system in a state where the kernel can be dumped and patched
  - XOM/xotext is disabled, write is enabled
  - Upon resume, it will print the first 8 bytes of the kernel .text in a notification to demonstrate XOM defeat
- This does not ship with any patches (HEN or otherwise)
  - Consider this a bring your own patches scenario, we didn't feel like doing this.
- This payload needs to be aware of whether or not the console is a pro or a non-pro unit
  - Sony decided to waste their added 2GB precious DDR5 that pro units have on the hypervisor, so it is mapped in a different physical address range
    - `0x062800000` PA for non-pro units
    - `0x8E0800000` PA for pro units
  - Open an issue if the payload mis-identifies what your system is, and note the hex value printed in brackets in the notification
- If the system shuts down when resuming from rest mode, it means the exploit failed
  - If it failed, likely there's something wrong with the payload and your situation is not supported, this exploit should be 99.9% stable

## Build notes
This exploit payload is built using the [PS5-Payload-Dev SDK](https://github.com/ps5-payload-dev/sdk). Note also that the build for `break_hv_payload/` is slightly special, as it gets compiled to a flat binary of minimal size. The entirety of code in `break_hv_payload/` runs in supervisor/kernel mode.

The SDK must be available in `PS5_PAYLOAD_SDK`:

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make
```

`make` produces `byepervisor_reborn.elf`, which is what gets sent to the
console via `elfldr`.

### Checks

`make check` runs the offset table validator and then builds the payload.

The tables in `include/offsets/` are positional arrays indexed by the `ksym_t`
and `kgadget_t` enums in `include/kdlsym.h`. Because the order of the entries
only exists in the trailing comments, the compiler cannot check it, and a
table that is out of order still builds and still passes every bounds check
before reading the wrong kernel offset at runtime. `tools/check_offsets.py`
verifies that:

- every table lists its entries in the same order as the enums
- every table has exactly one entry per enum value, and no zero offsets
- every table in `include/offsets/` is `#include`d by `kdlsym.c`
- every table in `include/offsets/` is dispatched by the switches in `kdlsym.c`
- every version dispatched in `kdlsym.c` has a table

The last two checks exist because a table added without its matching
`#include` leaves the symbols undeclared and the project unbuildable.

## Adding firmware support
To add support for a different firmware, the main thing that needs to be added is the header file in `include/offsets/`. This includes all the kernel offsets that are needed.

Once the offsets have been added, the following functions need to be modified to add cases to the switch statements:
- `kgadget()`
- `kdlsym()`
- `rawoff()`

The new header also needs to be added to the `#include` list at the top of
`kdlsym.c`, otherwise the offsets will not compile. `make check-offsets` will
tell you if any of this was missed.

Some firmwares might require more extensive modifications if gadgets don't exist for them. 6.xx for example likely will not have some gadgets that are used on 13.xx.

## How to use
1. Run kernel exploit chain and get to elfldr
2. Send `byepervisor_reborn.elf`
3. Follow the directions of the notifications
