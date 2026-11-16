#pragma once

#include <stdint.h>
typedef struct {uint32_t PECR,PEKEYR,SR;} flash_t;
extern flash_t *FLASH;
#define FLASH_SR_WRPERR 0
#define FLASH_SR_PGAERR 0
#define FLASH_SR_SIZERR 0
#define FLASH_SR_NOTZEROERR 0
#define FLASH_SR_FWWERR 0
#define FLASH_SR_EOP 0
#define FLASH_SR_BSY 0
#define FLASH_PECR_PELOCK 0
typedef struct {uint32_t MODER,OTYPER,OSPEEDR,PUPDR,BSRR,BRR;} gpio_t;
extern gpio_t *GPIOA;
