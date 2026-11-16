/* Overrides of the nRF5 SDK 15.0.0 ble_app_hids_keyboard (pca10040/s132) sdk_config.h, which the
 * Makefile includes from the SDK; only the settings ECK's Bluetooth bridge changes are here. */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* SPI slave link to the STM32 (SPIS1) */
#define NRFX_SPIS_ENABLED 1
#define NRFX_SPIS1_ENABLED 1
#define SPIS_ENABLED 1
#define SPIS1_ENABLED 1

/* No UART on the Hybrid: logs go to RTT */
#define UART_ENABLED 0
#define NRF_LOG_BACKEND_UART_ENABLED 0
#define NRF_LOG_BACKEND_RTT_ENABLED 1

/* Vendor UUID base for the battery-voltage service */
#define NRF_SDH_BLE_VS_UUID_COUNT 0

/* LE Secure Connections: P-256 ECDH in Nordic's Oberon library, random numbers from the
 * RNG through the SoftDevice-safe driver */
#define BLE_LESC_ENABLED 1
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

/* Module datasheet: its 32.768 kHz crystal does not hold +/-50 ppm over temperature. */
#define NRF_SDH_CLOCK_LF_ACCURACY 3 /* 100 ppm */

#endif
