# ESP-Hosted (C6 Wi-Fi) local patches: VENDORED

Since 2026-11-17, esp_hosted 2.12.0 is vendored in `components/espressif__esp_hosted/`
as the pristine registry copy plus `patches/esp_hosted-2.12.0-sdio-rx-bound.patch`, and
committed. Before that, these edits lived only in the git-ignored `managed_components/`
and were silently lost on every component re-fetch.

When updating esp_hosted: download the new version from the registry, re-apply the
patch (or confirm upstream fixed it), and regenerate the patch file.

File: `components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c`
Function: `sdio_read_task()` RX path (~line 1150).

## Why (root cause, verified 2026-07-05)

SDIO runs in **streaming RX mode** (`CONFIG_ESP_HOSTED_SDIO_OPTIMIZATION_RX_STREAMING_MODE=y`),
where `len_from_slave` is the **aggregate of multiple queued packets** in one CMD53
burst — routinely larger than a single 1536-byte transport buffer (observed 2480).
`sdio_rx_get_buffer()` `malloc()`s exactly that length, so 2480 is fine.

An earlier local patch bounded the length at `MAX_TRANSPORT_BUFFER_SIZE` (1536) to
stop a crash from *absurd* garbage lengths (e.g. 820736 → malloc-fail → `assert`).
That bound was too tight: it dropped every legitimate aggregated stream → Wi-Fi RX
broke → all TLS (NWS weather fetches, HTTP under load) failed while tiny packets
(SNTP) still worked. Symptom set: "weather never loads", no radar overlay, flaky
host↔device HTTP.

## The two edits

**1. Bound at RX queue capacity, not a single buffer.** RX queue = 20 buffers × 1536
≈ 30 KB, so bound at 32 KB — still rejects 820736-class garbage, passes legit streams:

```c
// was: if (ret || !len_from_slave || len_from_slave > MAX_TRANSPORT_BUFFER_SIZE) {
if (ret || !len_from_slave || len_from_slave > (32 * 1024)) {
    ESP_LOGW(TAG, "invalid ret or len_from_slave: %d %ld", ret, len_from_slave);
    SDIO_DRV_UNLOCK();
    continue;
} else {
    ESP_LOGD(TAG, "len_from_slave: %ld", len_from_slave);
}
```

**2. Drop instead of `assert()` on RX alloc failure**, so a large in-bound length under
internal-DRAM pressure glitches the link instead of crashing:

```c
rxbuff = sdio_rx_get_buffer(len_from_slave);
// was: assert(rxbuff);
if (!rxbuff) {
    ESP_LOGW(TAG, "rx buffer alloc failed for len %ld — dropping", len_from_slave);
    SDIO_DRV_UNLOCK();
    continue;
}
```

## Verify after reapply

Boot, wait for the NWS conditions fetch (~100 s), check serial:
- `nws_cond: conditions: <temp> "<desc>" …` and `state_weather: … (conditions=valid)`
- **zero** `invalid ret or len_from_slave` warnings
- HTTP requests to the device succeed reliably

The C6 firmware itself is fine — this is purely a host-side buffer-bound issue. A C6
reflash is NOT required. C6 SDIO clock is 40 MHz (correct).
