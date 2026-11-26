#include "nrf_update.h"

#include <string.h>

#include "ble_link.h"
#include "nrf.h"
#include "nrf_fstorage.h"
#include "nrf_fstorage_sd.h"
#include "nrf_sdh.h"

/* PFU's bootloader copies image bytes from STAGE_BASE to 0x1000 when the record at RECORD_ADDR
 * holds {size, 0, md5, crc16-modbus of the first 24 bytes}: hybrid-stock-behaviour.md (nRF52 staging). */
#define RECORD_ADDR 0x36000UL
#define STAGE_BASE 0x37000UL
#define IMAGE_MAX 0x35000UL /* the bootloader erases 0x1000..0x36000 only */
#define PAGE 0x1000UL
#define IMAGE_BASE 0x1000UL /* where the bootloader copies the staged image to */
#define APP_ADDR 0x26000UL   /* ECK's app (hybrid_ble_gcc_nrf52.ld) */

static uint8_t m_state;
static uint32_t m_size, m_staged, m_erase_next, m_erase_end;
static uint8_t m_md5[16];
static uint32_t m_word = 0xFFFFFFFFUL;
static uint8_t m_word_len;

uint8_t nrf_update_state(void) {
    return m_state;
}

uint32_t nrf_update_staged(void) {
    return m_staged;
}

static void nvmc_wait(void) {
    while (!NRF_NVMC->READY) {}
}

static void flash_erase(uint32_t addr) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    nvmc_wait();
    NRF_NVMC->ERASEPAGE = addr;
    nvmc_wait();
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmc_wait();
}

static void flash_word(uint32_t addr, uint32_t value) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    nvmc_wait();
    *(volatile uint32_t *)addr = value;
    nvmc_wait();
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmc_wait();
}

void nrf_update_start(uint32_t size, const uint8_t md5[16]) {
    if (size == 0 || size > IMAGE_MAX || (size & 3U)) {
        m_state = BLE_LINK_UPD_ST_BAD_START;
        return;
    }
    m_size = size;
    memcpy(m_md5, md5, sizeof(m_md5));
    m_staged = 0;
    m_word = 0xFFFFFFFFUL;
    m_word_len = 0;
    /* Record page first (no record: the bootloader ignores a half-staged image), then staging;
     * erased in nrf_update_poll so the link keeps answering. */
    m_erase_next = RECORD_ADDR;
    m_erase_end = STAGE_BASE + ((size + PAGE - 1U) & ~(PAGE - 1U));
    m_state = BLE_LINK_UPD_ST_ERASING;
}

static void finish_now(void);

void nrf_update_poll(void) {
    if (m_state == BLE_LINK_UPD_ST_VERIFYING) finish_now();
    if (m_state != BLE_LINK_UPD_ST_ERASING) return;
    if (nrf_sdh_is_enabled()) return; /* the SoftDevice may postpone its shutdown (pending flash) */
    flash_erase(m_erase_next);
    m_erase_next += PAGE;
    if (m_erase_next >= m_erase_end) m_state = BLE_LINK_UPD_ST_RECEIVING;
}

bool nrf_update_data(uint32_t offset, const uint8_t *data, uint8_t len) {
    if (m_state == BLE_LINK_UPD_ST_ERASING) return false;
    if (m_state != BLE_LINK_UPD_ST_RECEIVING) return true;
    if (offset + len <= m_staged) return true; /* a resent frame */
    if (offset != m_staged || m_staged + len > m_size) {
        m_state = BLE_LINK_UPD_ST_BAD_OFFSET;
        return true;
    }
    for (unsigned i = 0; i < len; i++) {
        m_word &= ~(0xFFUL << (8U * m_word_len));
        m_word |= (uint32_t)data[i] << (8U * m_word_len);
        if (++m_word_len == 4U) {
            flash_word(STAGE_BASE + m_staged + i - 3U, m_word);
            m_word = 0xFFFFFFFFUL;
            m_word_len = 0;
        }
    }
    m_staged += len;
    return true;
}

/* ---------- MD5 (RFC 1321) and CRC-16/MODBUS ---------- */

static uint32_t rol(uint32_t x, unsigned c) {
    return (x << c) | (x >> (32U - c));
}

static void md5_block(uint32_t h[4], const uint8_t *p) {
    static const uint8_t r[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    uint32_t w[16];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i + 2] << 16 |
               (uint32_t)p[4 * i + 3] << 24;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t f;
        unsigned g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + rol(a + f + k[i] + w[g], r[(i / 16) * 4 + i % 4]);
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

static void md5(const uint8_t *data, uint32_t len, uint8_t out[16]) {
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    uint8_t buf[64];
    uint32_t i = 0;
    for (; i + 64 <= len; i += 64) md5_block(h, data + i);
    uint32_t rest = len - i;
    memcpy(buf, data + i, rest);
    buf[rest++] = 0x80;
    if (rest > 56) {
        memset(buf + rest, 0, 64 - rest);
        md5_block(h, buf);
        rest = 0;
    }
    memset(buf + rest, 0, 56 - rest);
    uint64_t bits = (uint64_t)len * 8U;
    for (unsigned b = 0; b < 8; b++) buf[56 + b] = (uint8_t)(bits >> (8 * b));
    md5_block(h, buf);
    for (unsigned w = 0; w < 4; w++) {
        for (unsigned b = 0; b < 4; b++) out[4 * w + b] = (uint8_t)(h[w] >> (8 * b));
    }
}

static uint16_t crc16_modbus(const uint8_t *p, unsigned n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; b++) crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

/* The MD5 runs from the main loop, after the frame has been acknowledged. */
void nrf_update_finish(void) {
    if (m_state == BLE_LINK_UPD_ST_RECEIVING) m_state = BLE_LINK_UPD_ST_VERIFYING;
}

static void finish_now(void) {
    if (m_word_len) flash_word(STAGE_BASE + m_staged - m_word_len, m_word); /* FF-padded */
    m_word_len = 0;
    if (m_staged != m_size) {
        m_state = BLE_LINK_UPD_ST_BAD_OFFSET;
        return;
    }
    uint8_t sum[16];
    md5((const uint8_t *)STAGE_BASE, m_size, sum);
    if (memcmp(sum, m_md5, sizeof(sum)) != 0) {
        m_state = BLE_LINK_UPD_ST_BAD_MD5;
        return;
    }
    uint32_t rec[7] = {m_size, 0};
    memcpy(&rec[2], sum, sizeof(sum));
    rec[6] = crc16_modbus((const uint8_t *)rec, 24);
    for (unsigned i = 0; i < 7; i++) flash_word(RECORD_ADDR + 4U * i, rec[i]);
    m_state = BLE_LINK_UPD_ST_STAGED;
}

/* ---------- Bluetooth updates (SoftDevice running) ---------- */

static void fs_evt(nrf_fstorage_evt_t *p_evt);
NRF_FSTORAGE_DEF(nrf_fstorage_t m_bupd_fs) = {
    .evt_handler = fs_evt,
    .start_addr = RECORD_ADDR,
    .end_addr = STAGE_BASE + IMAGE_MAX,
};

static uint8_t m_b_state, m_b_err;
static uint32_t m_b_size;
static uint8_t m_b_md5[16];
static uint16_t m_b_done;                 /* pages committed */
static uint8_t m_b_target;                /* NRF_BUPD_TARGET_* */
static uint16_t m_b_stm_crc;
static uint32_t m_b_page = 0xFFFFFFFFUL;  /* page assembled in m_b_buf */
static bool m_b_busy;                     /* an fstorage operation is pending */
static uint32_t m_b_buf[PAGE / 4];        /* fstorage writes from it asynchronously */
static uint32_t m_b_rec[7];
static uint16_t m_b_copy_left; /* pages still to copy from live flash after the current one */

static void b_write_page(uint32_t page);

static void b_fail(uint8_t err) {
    m_b_state = NRF_BUPD_ERROR;
    m_b_err = err;
    m_b_copy_left = 0;
}

static void fs_evt(nrf_fstorage_evt_t *p_evt) {
    m_b_busy = false;
    if (m_b_state == NRF_BUPD_IDLE) return; /* aborted meanwhile */
    if (p_evt->result != NRF_SUCCESS) {
        b_fail(p_evt->id == NRF_FSTORAGE_EVT_ERASE_RESULT ? NRF_BUPD_E_ERASE : NRF_BUPD_E_WRITE);
    } else if (m_b_state == NRF_BUPD_ERASING) {
        m_b_state = NRF_BUPD_READY;
    } else if (m_b_state == NRF_BUPD_WRITING) {
        m_b_done++;
        m_b_state = NRF_BUPD_READY;
        if (m_b_copy_left) {
            m_b_copy_left--;
            memcpy(m_b_buf, (const void *)(IMAGE_BASE + m_b_done * PAGE), PAGE);
            b_write_page(m_b_done);
        }
    } else if (m_b_state == NRF_BUPD_RECORDING) {
        m_b_state = NRF_BUPD_STAGED;
    }
}

void nrf_bupd_init(void) {
    (void)nrf_fstorage_init(&m_bupd_fs, &nrf_fstorage_sd, NULL);
}

uint8_t nrf_bupd_state(void) {
    return m_b_state;
}

uint8_t nrf_bupd_error(void) {
    return m_b_err;
}

uint16_t nrf_bupd_done(void) {
    return m_b_done;
}

uint8_t nrf_bupd_target(void) {
    return m_b_target;
}

uint32_t nrf_bupd_size(void) {
    return m_b_size;
}

uint16_t nrf_bupd_stm_crc(void) {
    return m_b_stm_crc;
}

void nrf_bupd_stm_result(uint8_t err) {
    if (m_b_state != NRF_BUPD_STM_READY) return;
    if (err) b_fail(err == 7 ? NRF_BUPD_E_STUB : err == 8 ? NRF_BUPD_E_MODEL : NRF_BUPD_E_STM32);
    else m_b_state = NRF_BUPD_STM_DONE;
}

/* Erases the record page and the staging pages in one go: an 85 ms page erase fits only between
 * connection events skipped through slave latency, which the caller leaves on meanwhile. */
void nrf_bupd_begin(uint32_t size, const uint8_t md5[16], uint8_t target, uint16_t stm_crc) {
    m_b_target = target;
    m_b_stm_crc = stm_crc;
    if (m_b_busy) { b_fail(NRF_BUPD_E_BUSY); return; }
    if (size == 0 || size > IMAGE_MAX || (size & 3U)) { b_fail(NRF_BUPD_E_SIZE); return; }
    m_b_size = size;
    memcpy(m_b_md5, md5, sizeof(m_b_md5));
    m_b_done = 0;
    m_b_page = 0xFFFFFFFFUL;
    m_b_err = 0;
    uint32_t pages = 1U + (size + PAGE - 1U) / PAGE;
    if (nrf_fstorage_erase(&m_bupd_fs, RECORD_ADDR, pages, NULL) != NRF_SUCCESS) { b_fail(NRF_BUPD_E_ERASE); return; }
    m_b_busy = true;
    m_b_state = NRF_BUPD_ERASING;
}

/* Data for the page being assembled (page `done`); a new page starts blank. */
void nrf_bupd_data(uint32_t offset, const uint8_t *data, uint8_t len) {
    if (m_b_state != NRF_BUPD_READY) return;
    uint32_t page = offset / PAGE;
    if (page != m_b_done || offset % PAGE + len > PAGE || offset + len > m_b_size) { b_fail(NRF_BUPD_E_ORDER); return; }
    if (page != m_b_page) {
        memset(m_b_buf, 0xFF, sizeof(m_b_buf));
        m_b_page = page;
    }
    memcpy((uint8_t *)m_b_buf + offset % PAGE, data, len);
}

static void b_write_page(uint32_t page) {
    uint32_t len = m_b_size - page * PAGE;
    if (len > PAGE) len = PAGE;
    if (nrf_fstorage_write(&m_bupd_fs, STAGE_BASE + page * PAGE, m_b_buf, len, NULL) != NRF_SUCCESS) {
        b_fail(NRF_BUPD_E_WRITE);
        return;
    }
    m_b_busy = true;
    m_b_state = NRF_BUPD_WRITING;
}

void nrf_bupd_commit(uint32_t page, bool from_live) {
    nrf_bupd_copy(page, from_live ? 1 : 0);
}

/* count 0: commit the assembled page; else copy `count` pages from live flash, one after another. */
void nrf_bupd_copy(uint32_t page, uint16_t count) {
    if (m_b_state != NRF_BUPD_READY) return;
    uint32_t last = page + (count ? count : 1U) - 1U;
    if (page != m_b_done || last * PAGE >= m_b_size) { b_fail(NRF_BUPD_E_ORDER); return; }
    if (count) memcpy(m_b_buf, (const void *)(IMAGE_BASE + page * PAGE), PAGE);
    else if (m_b_page != page) memset(m_b_buf, 0xFF, sizeof(m_b_buf));
    m_b_page = 0xFFFFFFFFUL;
    m_b_copy_left = count ? (uint16_t)(count - 1U) : 0;
    b_write_page(page);
}

void nrf_bupd_finish(void) {
    if (m_b_state != NRF_BUPD_READY) return;
    if ((uint32_t)m_b_done * PAGE < m_b_size) { b_fail(NRF_BUPD_E_ORDER); return; }
    uint8_t sum[16];
    md5((const uint8_t *)STAGE_BASE, m_b_size, sum);
    if (memcmp(sum, m_b_md5, sizeof(sum)) != 0) { b_fail(NRF_BUPD_E_MD5); return; }
    if (m_b_target == NRF_BUPD_TARGET_STM32) { /* no record: PFU's bootloader must never install it */
        m_b_state = NRF_BUPD_STM_READY;
        return;
    }
    /* An ECK image: SoftDevice and app, the app's vectors where PFU's MBR jumps (0x26000). A wrong
     * image with a matching MD5 would otherwise leave Bluetooth to SWD. */
    const uint32_t *app = (const uint32_t *)(STAGE_BASE + APP_ADDR - IMAGE_BASE);
    if (m_b_size < APP_ADDR - IMAGE_BASE + 0x1000U || (app[0] >> 24) != 0x20U || app[1] < APP_ADDR ||
        app[1] >= IMAGE_BASE + IMAGE_MAX) {
        b_fail(NRF_BUPD_E_IMAGE);
        return;
    }
    memset(m_b_rec, 0, sizeof(m_b_rec));
    m_b_rec[0] = m_b_size;
    memcpy(&m_b_rec[2], sum, sizeof(sum));
    m_b_rec[6] = crc16_modbus((const uint8_t *)m_b_rec, 24);
    if (nrf_fstorage_write(&m_bupd_fs, RECORD_ADDR, m_b_rec, sizeof(m_b_rec), NULL) != NRF_SUCCESS) {
        b_fail(NRF_BUPD_E_WRITE);
        return;
    }
    m_b_busy = true;
    m_b_state = NRF_BUPD_RECORDING;
}

void nrf_bupd_refuse(uint8_t err) {
    if (m_b_state == NRF_BUPD_IDLE || m_b_state == NRF_BUPD_ERROR) b_fail(err);
}

/* Back to idle; a written record is erased (PFU's bootloader would install it at some reset). */
void nrf_bupd_abort(void) {
    if (m_b_state == NRF_BUPD_RECORDING || m_b_state == NRF_BUPD_STAGED) /* else PFU installs it at some reset */
        (void)nrf_fstorage_erase(&m_bupd_fs, RECORD_ADDR, 1, NULL);
    m_b_state = NRF_BUPD_IDLE;
    m_b_err = 0;
    m_b_copy_left = 0;
}

uint32_t nrf_bupd_live_crc(uint32_t page) {
    if (page >= IMAGE_MAX / PAGE) return 0; /* only the image: not the bootloader's or the bond pages */
    const uint8_t *p = (const uint8_t *)(IMAGE_BASE + page * PAGE);
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < PAGE; i++) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return ~crc;
}
