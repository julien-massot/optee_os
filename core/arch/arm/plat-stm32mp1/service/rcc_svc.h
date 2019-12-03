/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2017-2019, STMicroelectronics
 */

#ifndef __RCC_SVC_H__
#define __RCC_SVC_H__

uint32_t rcc_scv_handler(uint32_t x1, uint32_t x2, uint32_t x3);
uint32_t rcc_opp_scv_handler(uint32_t x1, uint32_t x2, uint32_t *res);

#endif /*__RCC_SVC_H__*/
