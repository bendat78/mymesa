/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>

#include "radv_private.h"
#include "addrlib/addrinterface.h"
#include "util/bitset.h"
#include "radv_amdgpu_winsys.h"
#include "radv_amdgpu_surface.h"
#include "sid.h"

#include "ac_surface.h"

static int radv_amdgpu_surface_sanity(const struct ac_surf_info *surf_info,
				      const struct radeon_surf *surf)
{
	unsigned type = RADEON_SURF_GET(surf->flags, TYPE);

	if (!(surf->flags & RADEON_SURF_HAS_TILE_MODE_INDEX))
		return -EINVAL;

	if (!surf->blk_w || !surf->blk_h)
		return -EINVAL;

	switch (type) {
	case RADEON_SURF_TYPE_1D:
		if (surf_info->height > 1)
			return -EINVAL;
		/* fall through */
	case RADEON_SURF_TYPE_2D:
	case RADEON_SURF_TYPE_CUBEMAP:
		if (surf_info->depth > 1 || surf_info->array_size > 1)
			return -EINVAL;
		break;
	case RADEON_SURF_TYPE_3D:
		if (surf_info->array_size > 1)
			return -EINVAL;
		break;
	case RADEON_SURF_TYPE_1D_ARRAY:
		if (surf_info->height > 1)
			return -EINVAL;
		/* fall through */
	case RADEON_SURF_TYPE_2D_ARRAY:
		if (surf_info->depth > 1)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

<<<<<<< HEAD
=======
static void *ADDR_API radv_allocSysMem(const ADDR_ALLOCSYSMEM_INPUT * pInput)
{
	return malloc(pInput->sizeInBytes);
}

static ADDR_E_RETURNCODE ADDR_API radv_freeSysMem(const ADDR_FREESYSMEM_INPUT * pInput)
{
	free(pInput->pVirtAddr);
	return ADDR_OK;
}

ADDR_HANDLE radv_amdgpu_addr_create(struct amdgpu_gpu_info *amdinfo, int family, int rev_id,
				    enum chip_class chip_class)
{
	ADDR_CREATE_INPUT addrCreateInput = {};
	ADDR_CREATE_OUTPUT addrCreateOutput = {};
	ADDR_REGISTER_VALUE regValue = {};
	ADDR_CREATE_FLAGS createFlags = {};
	ADDR_E_RETURNCODE addrRet;

	addrCreateInput.size = sizeof(ADDR_CREATE_INPUT);
	addrCreateOutput.size = sizeof(ADDR_CREATE_OUTPUT);

	regValue.noOfBanks = amdinfo->mc_arb_ramcfg & 0x3;
	regValue.gbAddrConfig = amdinfo->gb_addr_cfg;
	regValue.noOfRanks = (amdinfo->mc_arb_ramcfg & 0x4) >> 2;

	regValue.backendDisables = amdinfo->backend_disable[0];
	regValue.pTileConfig = amdinfo->gb_tile_mode;
	regValue.noOfEntries = ARRAY_SIZE(amdinfo->gb_tile_mode);
	if (chip_class == SI) {
		regValue.pMacroTileConfig = NULL;
		regValue.noOfMacroEntries = 0;
	} else {
		regValue.pMacroTileConfig = amdinfo->gb_macro_tile_mode;
		regValue.noOfMacroEntries = ARRAY_SIZE(amdinfo->gb_macro_tile_mode);
	}

	createFlags.value = 0;
	createFlags.useTileIndex = 1;

	addrCreateInput.chipEngine = CIASICIDGFXENGINE_SOUTHERNISLAND;
	addrCreateInput.chipFamily = family;
	addrCreateInput.chipRevision = rev_id;
	addrCreateInput.createFlags = createFlags;
	addrCreateInput.callbacks.allocSysMem = radv_allocSysMem;
	addrCreateInput.callbacks.freeSysMem = radv_freeSysMem;
	addrCreateInput.callbacks.debugPrint = 0;
	addrCreateInput.regValue = regValue;

	addrRet = AddrCreate(&addrCreateInput, &addrCreateOutput);
	if (addrRet != ADDR_OK)
		return NULL;

	return addrCreateOutput.hLib;
}

static int radv_compute_level(ADDR_HANDLE addrlib,
			      const struct radeon_surf_info *surf_info,
                              struct radeon_surf *surf, bool is_stencil,
                              unsigned level, unsigned type, bool compressed,
                              ADDR_COMPUTE_SURFACE_INFO_INPUT *AddrSurfInfoIn,
                              ADDR_COMPUTE_SURFACE_INFO_OUTPUT *AddrSurfInfoOut,
                              ADDR_COMPUTE_DCCINFO_INPUT *AddrDccIn,
                              ADDR_COMPUTE_DCCINFO_OUTPUT *AddrDccOut)
{
	struct radeon_surf_level *surf_level;
	ADDR_E_RETURNCODE ret;

	AddrSurfInfoIn->mipLevel = level;
	AddrSurfInfoIn->width = u_minify(surf_info->width, level);
	AddrSurfInfoIn->height = u_minify(surf_info->height, level);

	if (type == RADEON_SURF_TYPE_3D)
		AddrSurfInfoIn->numSlices = u_minify(surf_info->depth, level);
	else if (type == RADEON_SURF_TYPE_CUBEMAP)
		AddrSurfInfoIn->numSlices = 6;
	else
		AddrSurfInfoIn->numSlices = surf_info->array_size;

	if (level > 0) {
		/* Set the base level pitch. This is needed for calculation
		 * of non-zero levels. */
		if (is_stencil)
			AddrSurfInfoIn->basePitch = surf->stencil_level[0].nblk_x;
		else
			AddrSurfInfoIn->basePitch = surf->level[0].nblk_x;

		/* Convert blocks to pixels for compressed formats. */
		if (compressed)
			AddrSurfInfoIn->basePitch *= surf->blk_w;
	}

	ret = AddrComputeSurfaceInfo(addrlib,
				     AddrSurfInfoIn,
				     AddrSurfInfoOut);
	if (ret != ADDR_OK)
		return ret;

	surf_level = is_stencil ? &surf->stencil_level[level] : &surf->level[level];
	surf_level->offset = align64(surf->bo_size, AddrSurfInfoOut->baseAlign);
	surf_level->slice_size = AddrSurfInfoOut->sliceSize;
	surf_level->pitch_bytes = AddrSurfInfoOut->pitch * (is_stencil ? 1 : surf->bpe);
	surf_level->nblk_x = AddrSurfInfoOut->pitch;
	surf_level->nblk_y = AddrSurfInfoOut->height;
	if (type == RADEON_SURF_TYPE_3D)
		surf_level->nblk_z = AddrSurfInfoOut->depth;
	else
		surf_level->nblk_z = 1;

	switch (AddrSurfInfoOut->tileMode) {
	case ADDR_TM_LINEAR_ALIGNED:
		surf_level->mode = RADEON_SURF_MODE_LINEAR_ALIGNED;
		break;
	case ADDR_TM_1D_TILED_THIN1:
		surf_level->mode = RADEON_SURF_MODE_1D;
		break;
	case ADDR_TM_2D_TILED_THIN1:
		surf_level->mode = RADEON_SURF_MODE_2D;
		break;
	default:
		assert(0);
	}

	if (is_stencil)
		surf->stencil_tiling_index[level] = AddrSurfInfoOut->tileIndex;
	else
		surf->tiling_index[level] = AddrSurfInfoOut->tileIndex;

	surf->bo_size = surf_level->offset + AddrSurfInfoOut->surfSize;

	/* Clear DCC fields at the beginning. */
	surf_level->dcc_offset = 0;
	surf_level->dcc_enabled = false;

	/* The previous level's flag tells us if we can use DCC for this level. */
	if (AddrSurfInfoIn->flags.dccCompatible &&
	    (level == 0 || AddrDccOut->subLvlCompressible)) {
		AddrDccIn->colorSurfSize = AddrSurfInfoOut->surfSize;
		AddrDccIn->tileMode = AddrSurfInfoOut->tileMode;
		AddrDccIn->tileInfo = *AddrSurfInfoOut->pTileInfo;
		AddrDccIn->tileIndex = AddrSurfInfoOut->tileIndex;
		AddrDccIn->macroModeIndex = AddrSurfInfoOut->macroModeIndex;

		ret = AddrComputeDccInfo(addrlib,
					 AddrDccIn,
					 AddrDccOut);

		if (ret == ADDR_OK) {
			surf_level->dcc_offset = surf->dcc_size;
			surf_level->dcc_fast_clear_size = AddrDccOut->dccFastClearSize;
			surf_level->dcc_enabled = true;
			surf->dcc_size = surf_level->dcc_offset + AddrDccOut->dccRamSize;
			surf->dcc_alignment = MAX2(surf->dcc_alignment, AddrDccOut->dccRamBaseAlign);
		}
	}

	if (!is_stencil && AddrSurfInfoIn->flags.depth &&
	    surf_level->mode == RADEON_SURF_MODE_2D && level == 0) {
		ADDR_COMPUTE_HTILE_INFO_INPUT AddrHtileIn = {};
		ADDR_COMPUTE_HTILE_INFO_OUTPUT AddrHtileOut = {};
		AddrHtileIn.flags.tcCompatible = AddrSurfInfoIn->flags.tcCompatible;
		AddrHtileIn.pitch = AddrSurfInfoOut->pitch;
		AddrHtileIn.height = AddrSurfInfoOut->height;
		AddrHtileIn.numSlices = AddrSurfInfoOut->depth;
		AddrHtileIn.blockWidth = ADDR_HTILE_BLOCKSIZE_8;
		AddrHtileIn.blockHeight = ADDR_HTILE_BLOCKSIZE_8;
		AddrHtileIn.pTileInfo = AddrSurfInfoOut->pTileInfo;
		AddrHtileIn.tileIndex = AddrSurfInfoOut->tileIndex;
		AddrHtileIn.macroModeIndex = AddrSurfInfoOut->macroModeIndex;

		ret = AddrComputeHtileInfo(addrlib,
		                           &AddrHtileIn,
		                           &AddrHtileOut);

		if (ret == ADDR_OK) {
			surf->htile_size = AddrHtileOut.htileBytes;
			surf->htile_slice_size = AddrHtileOut.sliceSize;
			surf->htile_alignment = AddrHtileOut.baseAlign;
		}
	}
	return 0;
}

static void radv_set_micro_tile_mode(struct radeon_surf *surf,
                                     struct radeon_info *info)
{
	uint32_t tile_mode = info->si_tile_mode_array[surf->tiling_index[0]];

	if (info->chip_class >= CIK)
		surf->micro_tile_mode = G_009910_MICRO_TILE_MODE_NEW(tile_mode);
	else
		surf->micro_tile_mode = G_009910_MICRO_TILE_MODE(tile_mode);
}

static unsigned cik_get_macro_tile_index(struct radeon_surf *surf)
{
	unsigned index, tileb;

	tileb = 8 * 8 * surf->bpe;
	tileb = MIN2(surf->tile_split, tileb);

	for (index = 0; tileb > 64; index++)
		tileb >>= 1;

	assert(index < 16);
	return index;
}

static int radv_amdgpu_winsys_surface_init(struct radeon_winsys *_ws,
					   const struct ac_surf_info *surf_info,
					   struct radeon_surf *surf)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);

	unsigned mode, type;

	int r;

	r = radv_amdgpu_surface_sanity(surf_info, surf);
	if (r)
		return r;

	type = RADEON_SURF_GET(surf->flags, TYPE);
	mode = RADEON_SURF_GET(surf->flags, MODE);

	struct ac_surf_config config;

	memcpy(&config.info, surf_info, sizeof(config.info));
	config.is_3d = !!(type == RADEON_SURF_TYPE_3D);
	config.is_cube = !!(type == RADEON_SURF_TYPE_CUBEMAP);

	return ac_compute_surface(ws->addrlib, &ws->info, &config, mode, surf);
}

static int radv_amdgpu_winsys_surface_best(struct radeon_winsys *rws,
					   struct radeon_surf *surf)
{
	return 0;
}

void radv_amdgpu_surface_init_functions(struct radv_amdgpu_winsys *ws)
{
	ws->base.surface_init = radv_amdgpu_winsys_surface_init;
	ws->base.surface_best = radv_amdgpu_winsys_surface_best;
}
