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

#define PTA_OCRAM_UUID \
	{ 0xf03fea1d,  \
	  0xa4db,      \
	  0x41c9,      \
	  { 0xa1, 0x9f, 0x86, 0x99, 0x89, 0xa0, 0x33, 0xbd } }

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
#define PTA_OCRAM_CMD_ALLOC 0

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
#define PTA_OCRAM_CMD_FREE 1
#endif /* __PTA_IMX_OCRAM_H__ */
