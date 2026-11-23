#ifndef HHKB_BLE_LINK_H
#define HHKB_BLE_LINK_H

/* SPI link between the Hybrid's STM32 (ECK, SPI master) and nRF52 (nrf52/, SPI slave).
 * Shared by both builds: bump BLE_LINK_VERSION on any change, since the two chips update separately.
 * Wiring: CS PA15/P0.22, SCK PB3/P0.25, MOSI PB5/P0.23, MISO PB4/P0.24. PB6/P0.18: held high while
 * the nRF52 starts, it selects recovery mode (no Bluetooth, only update frames); otherwise low.
 * Every transaction is one 32-byte frame each way, SPI mode 0, MSB first.
 * The nRF52 answers each transaction with the status it prepared after the previous one,
 * so a frame's acknowledgement arrives on the next transaction. */

#include <stdint.h>

#define BLE_LINK_FRAME_LEN 32U
#define BLE_LINK_DATA_MAX  27U
#define BLE_LINK_VERSION   7U

#define BLE_LINK_MAGIC_TO_NRF 0xA5U
#define BLE_LINK_MAGIC_TO_STM 0x5AU

/* STM32 -> nRF52 frame types */
enum {
    BLE_LINK_NOP = 0,      /* poll for status only */
    BLE_LINK_KEYBOARD = 1, /* 8-byte boot keyboard report: mods, reserved, 6 keys */
    BLE_LINK_CONSUMER = 2, /* 2-byte consumer usage (0 = released) */
    BLE_LINK_BATTERY = 3,  /* percent (u8) */
    BLE_LINK_COMMAND = 4,  /* one of BLE_LINK_CMD_* */
    BLE_LINK_UPDATE = 5,   /* data[0] = BLE_LINK_UPD_*: stage a new nRF52 image */
};

/* nRF52 update. START: size (u32 LE), MD5[16]; stops the SoftDevice (reset to recover).
 * DATA: offset (u32 LE), up to BLE_LINK_UPD_CHUNK bytes. FINISH: verify, write the record. */
enum {
    BLE_LINK_UPD_START = 1,
    BLE_LINK_UPD_DATA = 2,
    BLE_LINK_UPD_FINISH = 3,
};
#define BLE_LINK_UPD_CHUNK 22U
enum {
    BLE_LINK_UPD_ST_IDLE = 0,
    BLE_LINK_UPD_ST_ERASING = 1,
    BLE_LINK_UPD_ST_RECEIVING = 2,
    BLE_LINK_UPD_ST_STAGED = 3, /* PFU's bootloader installs it at the next nRF52 reset */
    BLE_LINK_UPD_ST_VERIFYING = 4,
    BLE_LINK_UPD_ST_BAD_START = 0x81,
    BLE_LINK_UPD_ST_BAD_OFFSET = 0x82,
    BLE_LINK_UPD_ST_BAD_MD5 = 0x83,
};

enum {
    BLE_LINK_CMD_PAIR = 1,        /* data[1] = slot: select it, forget its host, pair a new one */
    BLE_LINK_CMD_CLEAR_BONDS = 2, /* forget all hosts */
    BLE_LINK_CMD_DISCONNECT = 3,
    BLE_LINK_CMD_SELECT_SLOT = 4, /* data[1] = host slot 1..BLE_LINK_SLOTS */
    BLE_LINK_CMD_PAIR_CANCEL = 5, /* end the pairing window */
    BLE_LINK_CMD_DELETE_SLOT = 6, /* data[1] = slot: forget its host */
    BLE_LINK_CMD_PASSKEY = 7,     /* data[1..6] = ASCII digits typed for the host's passkey */
    BLE_LINK_CMD_POWER_OFF = 8,   /* disconnect, stop advertising, System OFF; PD2 reset restarts it */
};
#define BLE_LINK_SLOTS 4U

/* nRF52 -> STM32 frame type and status flags. Status data: flags, host LEDs, version, queue free,
 * slot, bonded slots, update state, staged (u32); v4: interval, latency, timeout (u16 LE each,
 * as granted by the host: 1.25 ms, events, 10 ms units; 0 while not connected). */
#define BLE_LINK_STATUS 0x80U
enum {
    BLE_LINK_ST_CONNECTED = 1U << 0, /* link secured: reports reach the host */
    BLE_LINK_ST_ADVERTISING = 1U << 1,
    BLE_LINK_ST_QUEUE_FULL = 1U << 2, /* last frame refused: resend it later */
    BLE_LINK_ST_PAIRING = 1U << 3,    /* pairing window: advertising to any host */
    BLE_LINK_ST_PASSKEY = 1U << 4,    /* the host shows a passkey: type it, then Return */
    BLE_LINK_ST_RECOVERY = 1U << 5,   /* v6: recovery mode (PB6 high at start): no Bluetooth */
    BLE_LINK_ST_HOST_REJECTED = 1U << 6, /* v7: the slot's host failed our keys (forgot us); cleared by
                                          * a secured link, pairing or a slot change */
};

/* Status payload: data[0] flags, data[1] host LED report, data[2] version, data[3] free queue slots,
 * data[4] current host slot (0 until the STM32 selects one), data[5] bit n-1 = slot n has a host,
 * data[6] BLE_LINK_UPD_ST_*, data[7..10] staged bytes (u32 LE) */

typedef struct {
    uint8_t magic;
    uint8_t seq;  /* to nRF: frame sequence (never 0 for data); to STM: last accepted seq */
    uint8_t type;
    uint8_t len;
    uint8_t data[BLE_LINK_DATA_MAX];
    uint8_t check; /* 0xFF minus the byte sum of all preceding bytes */
} ble_link_frame_t;

_Static_assert(sizeof(ble_link_frame_t) == BLE_LINK_FRAME_LEN, "frame size");

static inline uint8_t ble_link_check(const ble_link_frame_t *f) {
    const uint8_t *p = (const uint8_t *)f;
    uint8_t sum = 0;
    for (unsigned i = 0; i < BLE_LINK_FRAME_LEN - 1U; i++) sum = (uint8_t)(sum + p[i]);
    return (uint8_t)(0xFFU - sum);
}

#endif
