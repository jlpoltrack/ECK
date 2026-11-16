**Documentation:** [Install](install.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [Models](models.md) • [VIA](via.md) • [Debug](debug.md)

# Build

Run builds from the repository root. The first argument picks the keyboard; `-c` cleans first; `-d` adds bench options, comma-separated. Output goes to `build/<variant>/`: `hhkb_ec_<variant>.elf`, `.bin` (raw Bank 2 image) and `.hfb` (PFU update container).

```bash
./build.sh hybrid    # HHKB Professional Hybrid: build/hybrid/hhkb_ec_hybrid.{bin,hfb}
./build.sh classic   # HHKB Professional Classic: build/classic/hhkb_ec_classic.{bin,hfb}
./build.sh type_s    # HHKB Classic Type-S: build/type_s/hhkb_ec_type_s.{bin,hfb}
```

For the Hybrid, `build.sh` first builds the nRF52 app (`nrf52/`), and the `.hfb` packs it with Nordic's S132 v6.0.0 SoftDevice (`../nRF5_SDK_15.0.0`); without the SDK only the STM32 `.bin` is built. Classic and Type-S share one image; only their `.hfb` model and version differ. Building does not flash the keyboard. Each build also links its `compile_commands.json` at `build/compile_commands.json` for clangd and IDEs (the last variant built wins).

Bench options (`-d`), for development only:

- `HHKB_ADC_DIAGNOSTICS`: per-key sensor readings over VIA for `tools/bench/key_depth.py`.
- `HHKB_SCAN_DIAGNOSTICS`: scan timing counters, printed by the Polling Rate Monitor and readable over SWD.
- `HHKB_HYBRID_TEST_CMD`: bridge commands over SWD for `tools/bench/hybrid_cmd.py` (Hybrid).

For example `./build.sh hybrid -d HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS`. Settings that used to be build options are runtime VIA settings: Rapid Trigger, custom calibration (off: PFU's factory thresholds) and ×4 ADC oversampling. A keyboard with no saved settings starts with custom calibration and Rapid Trigger off.

The STM32 build uses `-Wall -Wextra` and builds without warnings; keep it that way.

The generated HFB versions are A4.50 for Classic and A8.50 for Type-S. The container builder calculates both required CRC values and sets the matching PFU model identifier.

## HFB container and model variants

An HHKB `.hfb` is exactly 65,570 bytes:

```text
+---------------------------------------------------------------+
| Outer CRC-16/MCRF4XX, little-endian (2 bytes)                 |
+---------------------------------------------------------------+
| Inner CRC-16/MCRF4XX of the Bank 2 payload (2 bytes)          |
+---------------------------------------------------------------+
| Bank 2 firmware, padded with 0xFF (65,536 bytes)              |
+---------------------------------------------------------------+
| Trailer: 14 × 0xFF, model ID (6), BCD version (4), 6 × 0xFF   |
+---------------------------------------------------------------+
```

Both CRCs use reflected polynomial `0x8408`, initial value `0xFFFF`, and no final XOR. The inner CRC is payload-specific; it is not a fixed model tag. The image is linked for Bank 2 at `0x08010000`.

Classic and Classic Type-S use the same STM32L072RB, 4 × 15 matrix, 4051 multiplexers, and 64 KiB Bank 2 layout for this port. The firmware-facing differences are the PFU identifiers and version series:

| Parameter               | Classic      | Classic Type-S |
| ----------------------- | ------------ | -------------- |
| Model ID                | `AHHX01`     | `AHHS01`       |
| Version series          | `A4.xx`      | `A8.xx`        |
| Current ECK HFB version | A4.50        | A8.50          |
| Container size          | 65,570 bytes | 65,570 bytes   |

`tools/make_hfb.py` builds both variants and calculates their CRCs automatically. For a manual invocation, use `--model AHHX01 --version A4.50` or `--model AHHS01 --version A8.50`.

## Flash and RAM usage

Bank 2 provides 65,536 bytes of Flash and the STM32L072 20,480 bytes of RAM. Current images (2026-11-16):

| Variant | Raw Bank 2 image | RAM at link time |
| --- | --- | --- |
| Hybrid | 44,724 bytes (68.2 %) | 17,768 bytes (86.8 %) |
| Classic, Type-S | 35,540 bytes (54.2 %) | 16,600 bytes (81.1 %) |

RAM counts the code run from RAM (the key scan), `.data`, `.bss` (task stacks, queues, the 512-byte FreeRTOS heap, the keymap) and the 2 KB start-up stack; it is a build-time layout, not a measured worst-case stack depth. The 65,570-byte `.hfb` holds the padded Bank 2 payload and 34 bytes of container metadata (the Hybrid `.hfb` also carries the nRF52 app and SoftDevice).

## GitHub Actions

The [CI workflow](../.github/workflows/ci.yml) runs on every push and pull request: the host tests, then `./build.sh hybrid -c`, `./build.sh classic -c`, `./build.sh type_s -c` and the Hybrid boot stub (`make -C stm32/boot`). It has no nRF5 SDK, so its Hybrid build makes the STM32 `.bin` only. It publishes no artifacts: build locally to get firmware, and load [via.json](../stm32/via/via.json) in the [VIA web app](https://www.usevia.app/) Design tab for the HHKB Settings menu.
