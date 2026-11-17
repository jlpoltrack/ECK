# Third-party notices

ECK's own code is under the MIT licence in [LICENSE](LICENSE). The firmware files ECK builds and releases (`.bin`, `.hfb`) also contain the third-party software below, under these licences. This file ships with every release.

| Component | Where it is used | Licence |
| --- | --- | --- |
| FreeRTOS Kernel 11.3.3 (`stm32/vendor/freertos`) | STM32 firmware, all models | MIT |
| TinyUSB 0.21.0 subset (`stm32/vendor/tinyusb`) | STM32 firmware, all models | MIT |
| STM32L0 LL/HAL headers (`stm32/vendor/st/stm32l0xx_hal_driver`) | STM32 firmware, all models | BSD 3-Clause |
| STM32L0 CMSIS device files (`stm32/vendor/st/cmsis_device_l0`) | STM32 firmware, all models | Apache 2.0 |
| Arm CMSIS Core (`stm32/vendor/st/cmsis_core`; the nRF5 SDK's copy for the nRF52) | STM32 firmware, all models; Hybrid `.hfb` (nRF52) | Apache 2.0 |
| Nordic S132 SoftDevice 7.2.0 | Hybrid `.hfb` (nRF52) | Nordic 5-Clause |
| Nordic nRF5 SDK 17.1.1 libraries, nrfx drivers, Oberon crypto library | Hybrid `.hfb` (nRF52) | Nordic 5-Clause |
| nRF52 MDK startup and system files (Arm, modified by Nordic) | Hybrid `.hfb` (nRF52) | Apache 2.0 |
| SEGGER RTT 6.18a (in the nRF5 SDK) | Hybrid `.hfb` (nRF52) | SEGGER BSD-style |

The nRF5 SDK and SoftDevice are not in this repository; builds use Nordic's SDK download (see [README](README.md)). The Nordic-licensed parts may only be used with a Nordic Semiconductor integrated circuit (the Hybrid's nRF52832), and Nordic's binaries must not be reverse engineered, decompiled, modified or disassembled.

## MIT

FreeRTOS Kernel: Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved. Portions Copyright 2025 - 2026 Arm Limited and/or its affiliates.
TinyUSB: Copyright (c) 2012-2026, hathach (tinyusb.org). Portions Copyright (c) 2019 Nathan Conrad, (c) 2020 Reinhard Panhuber, (c) 2022 Simon Küppers, (c) 2022-2025 HiFiPhile (Zixun LI).

```text
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## BSD 3-Clause (STMicroelectronics)

Copyright 2017 STMicroelectronics. All rights reserved.

```text
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Apache 2.0 (Arm, STMicroelectronics, Nordic)

Arm CMSIS Core: Copyright (c) 2009-2022 Arm Limited. All rights reserved.
STM32L0 CMSIS device files: Copyright (c) 2016 STMicroelectronics.
nRF52 MDK startup and system files: Copyright (c) 2009-2021 ARM Limited. All rights reserved. Modified by Nordic Semiconductor ASA.

Licensed under the Apache License, Version 2.0. The full text is in [stm32/vendor/st/cmsis_device_l0/LICENSE.md](stm32/vendor/st/cmsis_device_l0/LICENSE.md) and at <https://www.apache.org/licenses/LICENSE-2.0>. Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

## Nordic 5-Clause (Nordic Semiconductor)

S132 SoftDevice: Copyright (c) 2007 - 2020, Nordic Semiconductor ASA. All rights reserved.
nRF5 SDK libraries, nrfx drivers and Oberon crypto library: Copyright (c) 2013 - 2021, Nordic Semiconductor ASA. All rights reserved.

```text
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form, except as embedded into a Nordic
   Semiconductor ASA integrated circuit in a product or a software update for
   such product, must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other
   materials provided with the distribution.

3. Neither the name of Nordic Semiconductor ASA nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

4. This software, with or without modification, must only be used with a
   Nordic Semiconductor ASA integrated circuit.

5. Any software provided in binary form under this license must not be reverse
   engineered, decompiled, modified and/or disassembled.

THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## SEGGER RTT

(c) 2014 - 2017 SEGGER Microcontroller GmbH & Co. KG. All rights reserved.

```text
Redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following
conditions are met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided
  with the distribution.

o Neither the name of SEGGER Microcontroller GmbH & Co. KG
  nor the names of its contributors may be used to endorse or
  promote products derived from this software without specific
  prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
```
