/* Overrides of the nRF5 SDK 17.1.1 ble_app_hids_keyboard (pca10040/s132) sdk_config.h, which the
 * Makefile includes from the SDK; only the settings ECK's Bluetooth bridge changes are here. */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* SPI slave link to the STM32 (SPIS1) */
#define NRFX_SPIS_ENABLED 1
#define NRFX_SPIS1_ENABLED 1
#define SPIS_ENABLED 1
#define SPIS1_ENABLED 1

/* No UART or buttons on the Hybrid: logs go to RTT, only in a `make NRF_LOG=1` build */
#define UART_ENABLED 0
#define NRFX_UART_ENABLED 0
#define NRFX_UARTE_ENABLED 0
#define GPIOTE_ENABLED 0
#define NRFX_GPIOTE_ENABLED 0
#define BSP_BTN_BLE_ENABLED 0
#define NRF_LOG_BACKEND_UART_ENABLED 0
#define NRF_LOG_BACKEND_RTT_ENABLED 1

/* No vendor-specific UUIDs: only standard services */
#define NRF_SDH_BLE_VS_UUID_COUNT 0

/* 244-byte update reports need MTU 247 (the Mac grants it, data length 251, 2M PHY: ble_probe.py).
 * Never enable NRF_SDH_BLE_SERVICE_CHANGED: it stopped Bluetooth typing on the Mac (AGENTS.md). */
#define NRF_SDH_BLE_GATT_MAX_MTU_SIZE 247
#define NRF_SDH_BLE_GAP_DATA_LENGTH 251
#define NRF_SDH_BLE_GATTS_ATTR_TAB_SIZE 2560
/* Bluetooth updates write flash while connected: small writes fit between connection events. */
#define NRF_FSTORAGE_SD_MAX_WRITE_SIZE 1024

/* LE Secure Connections through the peer manager: P-256 ECDH in Nordic's Oberon library,
 * random numbers straight from the RNG (no mbed TLS DRBG) */
#define PM_LESC_ENABLED 1
#define NRF_BLE_LESC_ENABLED 1
#define NRF_BLE_LESC_GENERATE_NEW_KEYS 1
#define NRF_CRYPTO_ENABLED 1
#define NRF_CRYPTO_BACKEND_OBERON_ENABLED 1
#define NRF_CRYPTO_BACKEND_OBERON_CHACHA_POLY_ENABLED 0
#define NRF_CRYPTO_BACKEND_OBERON_ECC_CURVE25519_ENABLED 0
#define NRF_CRYPTO_BACKEND_OBERON_ECC_ED25519_ENABLED 0
#define NRF_CRYPTO_BACKEND_OBERON_HASH_SHA256_ENABLED 0
#define NRF_CRYPTO_BACKEND_OBERON_HASH_SHA512_ENABLED 0
#define NRF_CRYPTO_BACKEND_OBERON_HMAC_SHA256_ENABLED 0
#define NRF_CRYPTO_BACKEND_OBERON_HMAC_SHA512_ENABLED 0
#define NRF_CRYPTO_BACKEND_NRF_HW_RNG_ENABLED 1
#define NRF_CRYPTO_BACKEND_NRF_HW_RNG_MBEDTLS_CTR_DRBG_ENABLED 0
#define RNG_ENABLED 1
#define NRFX_RNG_ENABLED 1
#define NRF_QUEUE_ENABLED 1
#define NRF_CRYPTO_RNG_AUTO_INIT_ENABLED 1
#define NRF_CRYPTO_RNG_STATIC_MEMORY_BUFFERS_ENABLED 1

/* Module crystal for the LF clock; its datasheet doesn't promise +/-50 ppm over temperature. */
#define NRF_SDH_CLOCK_LF_SRC 1
#define NRF_SDH_CLOCK_LF_ACCURACY 3 /* 100 ppm */

#endif
