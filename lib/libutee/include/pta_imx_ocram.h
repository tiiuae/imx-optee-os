/****************************************************************************
 *
 *   Copyright (c) 2025 Technology Innovation Institute. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#ifndef __PTA_IMX_OCRAM_H__
#define __PTA_IMX_OCRAM_H__

#include <stdint.h>

#define PTA_OCRAM_UUID \
	{ 0xf03fea1d,  \
	  0xa4db,      \
	  0x41c9,      \
	  { 0xa1, 0x9f, 0x86, 0x99, 0x89, 0xa0, 0x33, 0xbd } }

/*
 * AHAB-related definitions required by this PTA
 */

#define AHAB_IV_MAX_LEN 32
#define AHAB_HASH_MAX_LEN 64

/* i.MX AHAB container image header */
struct boot_img_hdr {
	uint32_t offset;
	uint32_t size;
	uint64_t dst;
	uint64_t entry;
	uint32_t hab_flags;
	uint32_t meta;
	uint8_t hash[AHAB_HASH_MAX_LEN];
	uint8_t iv[AHAB_IV_MAX_LEN];
} __packed;

/* following must match definitions of BL2 */
#define OCRAM_BOOTINFO_MAGIC 0x42414841 /* AHAB */
#define OCRAM_BOOTINFO_NB_IHDR 4

struct ocram_bootinfo_s {
	uint32_t magic;
	struct boot_img_hdr boot_hdrs[OCRAM_BOOTINFO_NB_IHDR];
	char bl2_version[32];
} __packed;

/**
 * Get the AHAB boot image headers and BL2 version stored in OCRAM and saved
 * by this PTA before any allocations are made.
 *
 * [out] memref[0].buffer	Output buffer to store the bootinfo_s struct
 * [out] memref[0].size		Size of the buffer
 *
 * Return codes:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_ACCESS_DENIED - Unexpected caller TA context
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input parameter
 * TEE_ERROR_NO_DATA - Boot info was not placed in OCRAM
 */
#define PTA_OCRAM_CMD_GET_BOOTINFO 0

/**
 * Allocate OCRAM memory
 *
 * [in]   params[0].value.a   Size of the buffer to allocate
 * [out]  params[1].value.a,b Output buffer allocated or NULL. `a` holds the
 *                            higher 32-bits, and `b` holds the lower 32-bits.
 *
 * Return codes:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_ACCESS_DENIED - Unexpected caller TA context
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input parameter
 * TEE_ERROR_OUT_OF_MEMORY - Out of OCRAM memory
 */
#define PTA_OCRAM_CMD_ALLOC 1

/*
 * Free OCRAM memory
 *
 * [in]  params[0].value.a,b Buffer to free. Can be NULL. `a` holds the higher
 *                           32-bits, and `b` holds the lower 32-bits.
 *
 * Return codes:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input parameter
 * TEE_ERROR_ITEM_NOT_FOUND - Specified memory not allocated to caller
 */
#define PTA_OCRAM_CMD_FREE 2

#endif /* __PTA_IMX_OCRAM_H__ */
