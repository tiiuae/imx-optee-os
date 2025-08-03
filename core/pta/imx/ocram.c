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

#include <string_ext.h>
#include <malloc.h>
#include <util.h>
#include <kernel/pseudo_ta.h>
#include <mm/core_mmu.h>
#include <mm/mobj.h>
#include <mm/tee_mm.h>
#include <mm/vm.h>
#include <sys/queue.h>
#include <pta_imx_ocram.h>

#define OCRAM_PTA_NAME "ocram.pta"

#define OCRAM_START 0x20518000
#define OCRAM_END 0x2051C000
#define OCRAM_SIZE (OCRAM_END - OCRAM_START)

/* Stash here boot info found in OCRAM upon creation */
static struct ocram_bootinfo_s g_bootinfo;

/* This is super wasteful but MMU hardcodes the page size to 4K so it's not
 * like we can use smaller granule and actually benefit from it. This is also
 * due to the fact that we do on-demand MMU mappings **per-allocation**. If
 * we wanted finer granularity we would need to maintain a mm pool per-TA,
 * commit in page sizes, and allocate in smaller chunks. Even then, we get
 * similar fragmentation with multiple-TAs (instead of multiple allocations
 * from the same TA).
 *
 * Can (and should) mitigate fragmentation by using a MM on the userspace.
 */
#define OCRAM_GRANULE_SHIFT SMALL_PAGE_SHIFT
#define OCRAM_GRANULE SMALL_PAGE_SIZE

struct mobj_oc {
	struct mobj mobj;
	paddr_t pa;
	vaddr_t user_va;
	tee_mm_entry_t *mm;
	struct user_mode_ctx *uctx;
	SLIST_ENTRY(mobj_oc) link;
};

SLIST_HEAD(alloc_list_t, mobj_oc);

static void *va_ocram_base = NULL;
static tee_mm_pool_t ocram_pool;

static const struct mobj_ops mobj_oc_ops;

static struct mobj_oc *to_mobj_oc(struct mobj *mobj)
{
	assert(mobj->ops == &mobj_oc_ops);
	return container_of(mobj, struct mobj_oc, mobj);
}

static struct mobj_oc *mobj_oc_new(struct user_mode_ctx *uctx, size_t size)
{
	TEE_Result res;
	struct mobj_oc *mo;
	void *va;

	mo = calloc(1, sizeof(struct mobj_oc));
	if (!mo)
		return NULL;

	mo->mm = tee_mm_alloc(&ocram_pool, size);
	if (!mo->mm)
		goto err_w_mo;

	mo->pa = tee_mm_get_smem(mo->mm);
	mo->uctx = uctx;
	mo->mobj.ops = &mobj_oc_ops;
	mo->mobj.phys_granule = OCRAM_GRANULE;
	mo->mobj.size = size;
	refcount_set(&mo->mobj.refc, 1);

	va = mo->mobj.ops->get_va(&mo->mobj, 0, size);
	memzero_explicit(va, size);

	res = vm_map(uctx, &mo->user_va, size, TEE_MATTR_URW | TEE_MATTR_PRW,
		     VM_FLAG_PERMANENT | VM_FLAG_SHAREABLE, &mo->mobj, 0);
	if (res != TEE_SUCCESS)
		goto err_w_mm;

	return mo;

err_w_mm:
	tee_mm_free(mo->mm);
err_w_mo:
	free(mo);
	return NULL;
}

static TEE_Result mobj_oc_get_pa(struct mobj *mobj, size_t offset,
				 size_t granule, paddr_t *pa)
{
	struct mobj_oc *mo = to_mobj_oc(mobj);
	paddr_t p = 0;

	if (!pa)
		return TEE_ERROR_GENERIC;

	if (offset >= mobj->size)
		return TEE_ERROR_GENERIC;

	p = mo->pa + offset;

	if (granule) {
		if (granule != OCRAM_GRANULE) {
			return TEE_ERROR_GENERIC;
		}
		p &= ~(granule - 1);
	}

	*pa = p;
	return TEE_SUCCESS;
}

static void *mobj_oc_get_va(struct mobj *mobj, size_t offset, size_t len)
{
	struct mobj_oc *mo = to_mobj_oc(mobj);

	if (!mobj_check_offset_and_len(mobj, offset, len))
		return NULL;

	return phys_to_virt(mo->pa + offset, MEM_AREA_RAM_SEC,
			    mobj->size - offset);
}

static TEE_Result mobj_oc_get_mem_type(struct mobj *mobj __unused,
				       uint32_t *mem_type)
{
	if (!mem_type)
		return TEE_ERROR_GENERIC;

	*mem_type = TEE_MATTR_MEM_TYPE_CACHED;
	return TEE_SUCCESS;
}

static bool mobj_oc_matches(struct mobj *mobj __unused, enum buf_is_attr attr)
{
	return attr == CORE_MEM_SEC;
}

static void mobj_oc_free(struct mobj *mobj)
{
	struct mobj_oc *mo;

	if (!mobj)
		return;

	mo = to_mobj_oc(mobj);

	if (!mo->uctx || !mo->user_va || !mo->mm)
		return;

	vm_unmap(mo->uctx, mo->user_va, mobj->size);

	tee_mm_free(mo->mm);

	memzero_explicit(mo, sizeof(struct mobj_oc));

	free(mo);
}

static const struct mobj_ops mobj_oc_ops = {
	.get_pa = mobj_oc_get_pa,
	.get_va = mobj_oc_get_va,
	.get_phys_offs = NULL,
	.get_mem_type = mobj_oc_get_mem_type,
	.matches = mobj_oc_matches,
	.free = mobj_oc_free,
};

static TEE_Result create(void)
{
	bool res;

	if (va_ocram_base)
		return TEE_SUCCESS;

	va_ocram_base =
		core_mmu_add_mapping(MEM_AREA_RAM_SEC, OCRAM_START, OCRAM_SIZE);
	if (!va_ocram_base)
		return TEE_ERROR_GENERIC;

	if (g_bootinfo.magic != OCRAM_BOOTINFO_MAGIC)
		memcpy(&g_bootinfo, va_ocram_base, sizeof(g_bootinfo));

	res = tee_mm_init(&ocram_pool, OCRAM_START, OCRAM_SIZE,
			  OCRAM_GRANULE_SHIFT, 0);
	if (!res) {
		tee_mm_final(&ocram_pool);
		core_mmu_remove_mapping(MEM_AREA_RAM_SEC, va_ocram_base,
					OCRAM_SIZE);
		va_ocram_base = NULL;
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	return TEE_SUCCESS;
}

static void destroy(void)
{
	if (!va_ocram_base)
		return;

	tee_mm_final(&ocram_pool);

	memzero_explicit(va_ocram_base, OCRAM_SIZE);

	core_mmu_remove_mapping(MEM_AREA_RAM_SEC, va_ocram_base, OCRAM_SIZE);

	va_ocram_base = NULL;
}

static TEE_Result open_session(uint32_t pt __unused,
			       TEE_Param params[TEE_NUM_PARAMS] __unused,
			       void **session)
{
	struct ts_session *ts = ts_get_current_session();
	struct tee_ta_session *ta_session;
	struct alloc_list_t *allocs;

	if (!ts || !ts->ctx)
		return TEE_ERROR_ACCESS_DENIED;

	ta_session = to_ta_session(ts);

	/* Only TAs allowed to use secure world OCRAM */

	if (ta_session->clnt_id.login != TEE_LOGIN_TRUSTED_APP)
		return TEE_ERROR_ACCESS_DENIED;

	allocs = calloc(1, sizeof(struct alloc_list_t));
	if (!allocs)
		return TEE_ERROR_OUT_OF_MEMORY;

	SLIST_INIT(allocs);
	*session = allocs;

	return TEE_SUCCESS;
}

static void close_session(void *sess_ctx)
{
	struct alloc_list_t *allocs = (struct alloc_list_t *)sess_ctx;
	struct mobj_oc *entry;

	while (!SLIST_EMPTY(allocs)) {
		entry = SLIST_FIRST(allocs);

		mobj_oc_free(&entry->mobj);

		SLIST_REMOVE_HEAD(allocs, link);
	}

	free(allocs);
}

static TEE_Result ocram_cmd_get_bootinfo(uint32_t param_types,
					 TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.size != sizeof(g_bootinfo))
		return TEE_ERROR_BAD_PARAMETERS;

	if (g_bootinfo.magic != OCRAM_BOOTINFO_MAGIC)
		return TEE_ERROR_NO_DATA;

	memcpy(params[0].memref.buffer, &g_bootinfo, sizeof(g_bootinfo));
	return TEE_SUCCESS;
}

static TEE_Result ocram_cmd_allocate(void *sess_ctx, uint32_t param_types,
				     TEE_Param params[TEE_NUM_PARAMS])
{
	struct alloc_list_t *allocs = (struct alloc_list_t *)sess_ctx;
	struct ts_session *ts = ts_get_calling_session();
	struct user_ta_ctx *utc;
	struct mobj_oc *entry;
	uint32_t size;
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						   TEE_PARAM_TYPE_VALUE_OUTPUT,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!sess_ctx || !ts || !ts->ctx)
		return TEE_ERROR_ACCESS_DENIED;

	utc = to_user_ta_ctx(ts->ctx);
	size = params[0].value.a;

	entry = mobj_oc_new(&utc->uctx, size);
	if (!entry)
		return TEE_ERROR_OUT_OF_MEMORY;

	SLIST_INSERT_HEAD(allocs, entry, link);

	reg_pair_from_64(entry->user_va, &params[1].value.a,
			 &params[1].value.b);

	return TEE_SUCCESS;
}

static TEE_Result ocram_cmd_free(void *sess_ctx, uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	struct alloc_list_t *allocs = (struct alloc_list_t *)sess_ctx;
	struct mobj_oc *entry;
	struct mobj_oc *entry_to_free = NULL;
	vaddr_t user_va;
	uint32_t exp_param_types =
		TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	user_va = reg_pair_to_64(params[0].value.a, params[0].value.b);

	if (user_va == 0)
		return TEE_SUCCESS;

	SLIST_FOREACH(entry, allocs, link) {
		if (entry->user_va == user_va) {
			entry_to_free = entry;
			break;
		}
	}

	if (!entry_to_free)
		return TEE_ERROR_ITEM_NOT_FOUND;

	SLIST_REMOVE(allocs, entry_to_free, mobj_oc, link);

	mobj_oc_free(&entry_to_free->mobj);

	return TEE_SUCCESS;
}

static TEE_Result invoke_command(void *sess_ctx, uint32_t cmd_id,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	switch (cmd_id) {
	case PTA_OCRAM_CMD_GET_BOOTINFO:
		return ocram_cmd_get_bootinfo(param_types, params);
	case PTA_OCRAM_CMD_ALLOC:
		return ocram_cmd_allocate(sess_ctx, param_types, params);
	case PTA_OCRAM_CMD_FREE:
		return ocram_cmd_free(sess_ctx, param_types, params);
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}

pseudo_ta_register(.uuid = PTA_OCRAM_UUID, .name = OCRAM_PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS, .create_entry_point = create,
		   .destroy_entry_point = destroy,
		   .open_session_entry_point = open_session,
		   .close_session_entry_point = close_session,
		   .invoke_command_entry_point = invoke_command);
