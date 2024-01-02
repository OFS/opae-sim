// Copyright(c) 2017-2022, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif				// HAVE_CONFIG_H

#include <opae/enum.h>
#include <opae/properties.h>
#include <opae/utils.h>
#include <opae/plugin.h>
#include "common_int.h"
#include "token.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ase_common.h"
uint32_t session_exist_status = NOT_ESTABLISHED;

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>

ase_afu_idx_mask ase_open_afus_by_tok_idx;

int aseNumTokens = 3;
struct _fpga_token aseToken[ASE_MAX_TOKENS] = {
	{
	  .idx = 0,
	  .hdr =
		{
			.magic = ASE_TOKEN_MAGIC,
			.vendor_id = 0x8086,
			.device_id = ASE_ID,
			.segment = 0,
			.bus = ASE_BUS,
			.device = ASE_DEVICE,
			.function = ASE_PF0_FUNCTION,
			.interface = FPGA_IFC_SIM_DFL,
			.objtype = FPGA_DEVICE,
			.object_id = ASE_PF0_FME_OBJID,
			.guid = { 0, },
			.subsystem_vendor_id = 0x8086,
			.subsystem_device_id = ASE_PF0_SUBSYSTEM_DEVICE
		},
	},
	{
	  .idx = 1,
	  .hdr =
		{
			.magic = ASE_TOKEN_MAGIC,
			.vendor_id = 0x8086,
			.device_id = ASE_ID,
			.segment = 0,
			.bus = ASE_BUS,
			.device = ASE_DEVICE,
			.function = ASE_PF0_FUNCTION,
			.interface = FPGA_IFC_SIM_DFL,
			.objtype = FPGA_ACCELERATOR,
			.object_id = ASE_PF0_PORT_OBJID,
			.guid = { 0, },
			.subsystem_vendor_id = 0x8086,
			.subsystem_device_id = ASE_PF0_SUBSYSTEM_DEVICE
		},
	},
	{
	  .idx = 2,
	  .hdr =
		{
			.magic = ASE_TOKEN_MAGIC,
			.vendor_id = 0x8086,
			.device_id = ASE_ID,
			.segment = 0,
			.bus = ASE_BUS,
			.device = ASE_DEVICE,
			.function = ASE_VF0_FUNCTION,
			.interface = FPGA_IFC_SIM_VFIO,
			.objtype = FPGA_ACCELERATOR,
			.object_id = ASE_VF0_PORT_OBJID,
			.guid = { 0, },
			.subsystem_vendor_id = 0x8086,
			.subsystem_device_id = ASE_VF0_SUBSYSTEM_DEVICE
		},
	}
};

extern ssize_t readlink(const char *, char *, size_t);
fpga_result ase_fpgaCloneToken(fpga_token src,
					fpga_token *dst);
fpga_result ase_fpgaUpdateProperties(fpga_token token, fpga_properties prop);
fpga_result ase_fpgaGetProperties(fpga_token token,
					   fpga_properties *prop);
void api_guid_to_fpga(uint64_t guidh, uint64_t guidl, uint8_t *guid)
{
	uint32_t i;
	uint32_t s;

	// The API expects the MSB of the GUID at [0] and the LSB at [15].
	s = 64;
	for (i = 0; i < 8; ++i) {
		s -= 8;
		guid[i] = (uint8_t) ((guidh >> s) & 0xff);
	}

	s = 64;
	for (i = 0; i < 8; ++i) {
		s -= 8;
		guid[8 + i] = (uint8_t) ((guidl >> s) & 0xff);
	}
}

STATIC bool matches_filter(struct _fpga_properties *filter, struct _fpga_token *tok)
{
	fpga_token_header *thdr = &tok->hdr;

	if (FIELD_VALID(filter, FPGA_PROPERTY_PARENT)) {
		fpga_token_header *parent_hdr =
			(fpga_token_header *)filter->parent;

		if (!parent_hdr) {
			return false; // Reject search based on NULL parent token
                }

                if (!fpga_is_parent_child(parent_hdr, thdr)) {
                        return false;
                }
        }

	if (FIELD_VALID(filter, FPGA_PROPERTY_OBJTYPE)) {
		if (filter->objtype != thdr->objtype) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_SEGMENT)) {
		if (filter->segment != thdr->segment) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_BUS)) {
		if (filter->bus != thdr->bus) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_DEVICE)) {
		if (filter->device != thdr->device) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_FUNCTION)) {
		if (filter->function != thdr->function) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_SOCKETID)) {
		if (filter->socket_id != ASE_SOCKET_ID) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_GUID)) {
		if (0 != memcmp(thdr->guid, filter->guid, sizeof(fpga_guid))) {
			//BEGIN_RED_FONTCOLOR;
			//printf("  [APP]  Filter mismatch\n");
			//END_RED_FONTCOLOR;
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_OBJECTID)) {
		if (filter->object_id != thdr->object_id) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_VENDORID)) {
		if (filter->vendor_id != thdr->vendor_id) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_DEVICEID)) {
		if (filter->device_id != thdr->device_id) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_SUB_VENDORID)) {
		if (filter->subsystem_vendor_id != thdr->subsystem_vendor_id) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_SUB_DEVICEID)) {
		if (filter->subsystem_device_id != thdr->subsystem_device_id) {
			return false;
		}
	}

	/* if (FIELD_VALID(filter, FPGA_PROPERTY_NUM_ERRORS)) */

	if (FIELD_VALID(filter, FPGA_PROPERTY_INTERFACE)) {
		if (filter->interface != thdr->interface) {
			return false;
		}
	}

	if (FIELD_VALID(filter, FPGA_PROPERTY_OBJTYPE)
	    && (FPGA_DEVICE == filter->objtype)) {

		if (FIELD_VALID(filter, FPGA_PROPERTY_NUM_SLOTS)) {
			if ((FPGA_DEVICE != thdr->objtype)
			    || (ASE_NUM_SLOTS
					!= filter->u.fpga.num_slots)) {
                                return false;
			}
		}

		if (FIELD_VALID(filter, FPGA_PROPERTY_BBSID)) {
			if ((FPGA_DEVICE != thdr->objtype)
			    || (ASE_BBSID
					!= filter->u.fpga.bbs_id)) {
                                return false;
			}
		}

		if (FIELD_VALID(filter, FPGA_PROPERTY_BBSVERSION)) {
			if ((FPGA_DEVICE != thdr->objtype)
			    || (ASE_BBS_VERSION_MAJOR
				!= filter->u.fpga.bbs_version.major)
			    || (ASE_BBS_VERSION_MINOR
				!= filter->u.fpga.bbs_version.minor)
			    || (ASE_BBS_VERSION_PATCH
				!= filter->u.fpga.bbs_version.patch)) {
				return false;
			}
		}

	} else if (FIELD_VALID(filter, FPGA_PROPERTY_OBJTYPE)
		   && (FPGA_ACCELERATOR == filter->objtype)) {

		fpga_accelerator_state state =
			(session_exist_status == NOT_ESTABLISHED) ?
			FPGA_ACCELERATOR_UNASSIGNED :
			FPGA_ACCELERATOR_ASSIGNED;

		if (FIELD_VALID(filter, FPGA_PROPERTY_ACCELERATOR_STATE)) {
			if ((FPGA_ACCELERATOR != thdr->objtype)
			    || (state
				!= filter->u.accelerator.state)) {
				return false;
			}
		}

		if (FIELD_VALID(filter, FPGA_PROPERTY_NUM_MMIO)) {
			if ((FPGA_ACCELERATOR != thdr->objtype)
			    || (ASE_NUM_MMIO
				!= filter->u.accelerator.num_mmio)) {
				return false;
			}
		}

		if (FIELD_VALID(filter, FPGA_PROPERTY_NUM_INTERRUPTS)) {
			if ((FPGA_ACCELERATOR != thdr->objtype)
			    || (ASE_NUM_IRQ
				!= filter->u.accelerator.num_interrupts)) {
				return false;
			}
		}
	}

	return true;
}

STATIC bool matches_filters(const fpga_properties *filter,
			    uint32_t num_filter,
			    fpga_token token)
{
	uint32_t i;
	struct _fpga_properties *_filter;
	struct _fpga_token *_tok;

	if (!filter || !num_filter) // no filter == match everything
		return true;

	_tok = (struct _fpga_token *)token;

	for (i = 0 ; i < num_filter ; ++i) {
		_filter = (struct _fpga_properties *)filter[i];
		if (matches_filter(_filter, _tok))
			return true;
	}

	return false;
}


fpga_result __FPGA_API__
ase_fpgaEnumerate(const fpga_properties *filters, uint32_t num_filters,
	      fpga_token *tokens, uint32_t max_tokens,
	      uint32_t *num_matches)
{
	uint64_t i;

	if ((num_filters > 0) && (NULL == (filters))) {
		return FPGA_INVALID_PARAM;
	}

	if (NULL == num_matches) {
		return FPGA_INVALID_PARAM;
	}

	if ((max_tokens > 0) && (NULL == tokens)) {
		return FPGA_INVALID_PARAM;
	}
	if (!num_filters && (NULL != filters)) {
		FPGA_MSG("num_filters == 0 with non-NULL filters");
		return FPGA_INVALID_PARAM;
	}

	if (session_exist_status == NOT_ESTABLISHED) {
		uint64_t afuid_data[2];
		fpga_guid readback_afuid;

		session_init();
		ase_memcpy(&aseToken[0].hdr.guid, FPGA_FME_GUID, sizeof(fpga_guid));

		// Fill in the token space by probing for AFU UUIDs on VFs.
		// The RTL simulation will return -1 after the last VF.
		// Start the search at token 2. Token 0 is the simulated FIM
		// and token 1 is the simulated management PF0.
		for (i = 2; i < ASE_MAX_TOKENS; i++) {
			mmio_read64(0x8, i-2, &afuid_data[0]);
			mmio_read64(0x10, i-2, &afuid_data[1]);

			// No more VFs?
			if (afuid_data[0] == INT64_C(-1) && afuid_data[1] == INT64_C(-1))
				break;

			// Only VF0's token is initialized in aseToken. Higher entries
			// need to be replicated from VF0, then adjust the function number.
			if (i > 2) {
				ase_memcpy(&aseToken[i], &aseToken[i-1], sizeof(struct _fpga_token));
				aseToken[i].hdr.function += 1;
				aseToken[i].idx += 1;
			}

			// Convert afuid_data to readback_afuid
			// e.g.: readback{0x5037b187e5614ca2, 0xad5bd6c7816273c2} -> "5037B187-E561-4CA2-AD5B-D6C7816273C2"
			api_guid_to_fpga(afuid_data[1], afuid_data[0], readback_afuid);
			// The VF contains the AFU.
			ase_memcpy(&aseToken[i].hdr.guid, readback_afuid, sizeof(fpga_guid));

			aseNumTokens = i + 1;

			ASE_INFO("Found AFU GUID 0x%016" PRIx64 " %016" PRIx64 " at device %02x:%02x:%x\n",
				 afuid_data[1], afuid_data[0],
				 aseToken[i].hdr.bus, aseToken[i].hdr.device, aseToken[i].hdr.function);
		}

		session_exist_status = ESTABLISHED;
	}

	*num_matches = 0;
	for (i = 0; i < aseNumTokens; i++) {
		// Skip AFUs that are already open
		if (!(ase_open_afus_by_tok_idx & (UINT64_C(1) << i))) {
			if (matches_filters(filters, num_filters, &aseToken[i])) {
				if (*num_matches < max_tokens)	{
					if (FPGA_OK != ase_fpgaCloneToken(&aseToken[i], &tokens[*num_matches]))
						FPGA_MSG("Error cloning token");
				}
				++*num_matches;
			}
		}
	}

	return FPGA_OK;
}

fpga_result __FPGA_API__ ase_fpgaDestroyToken(fpga_token *token)
{
	if (NULL == token || NULL == *token) {
		FPGA_MSG("Invalid token pointer");
		return FPGA_INVALID_PARAM;
	}

	struct _fpga_token *_token = (struct _fpga_token *)*token;

	if (_token->hdr.magic != ASE_TOKEN_MAGIC) {
		FPGA_MSG("Invalid token");
		return FPGA_INVALID_PARAM;
	}

	// invalidate magic (just in case)
	_token->hdr.magic = FPGA_INVALID_MAGIC;

	free(*token);
	*token = NULL;
	return FPGA_OK;

}

fpga_result __FPGA_API__ ase_fpgaGetPropertiesFromHandle(fpga_handle handle,
						     fpga_properties *prop)
{
	ASSERT_NOT_NULL(handle);
	struct _fpga_handle *_handle = (struct _fpga_handle *)handle;

	return ase_fpgaGetProperties(_handle->token, prop);
}

fpga_result __FPGA_API__ ase_fpgaGetProperties(fpga_token token,
					   fpga_properties *prop)
{
	struct _fpga_properties *_prop;
	fpga_result result = FPGA_OK;
	ASSERT_NOT_NULL(prop);
	//ASSERT_NOT_NULL(token);
	_prop = ase_malloc(sizeof(struct _fpga_properties));
	if (NULL == _prop) {
		FPGA_MSG("Failed to allocate memory for properties");
		return FPGA_NO_MEMORY;
	}
	ase_memset(_prop, 0, sizeof(struct _fpga_properties));
	// mark data structure as valid
	_prop->magic = FPGA_PROPERTY_MAGIC;

	if (token) {
		result = ase_fpgaUpdateProperties(token, _prop);
		if (result != FPGA_OK) {
			goto out_free;
		}
	}

	*prop = (fpga_properties) _prop;
	return result;
out_free:
	free(_prop);
	_prop = NULL;
	return result;
}

/* FIXME: make thread-safe? */
fpga_result __FPGA_API__ ase_fpgaCloneProperties(fpga_properties src,
					     fpga_properties *dst)
{
	struct _fpga_properties *_src = (struct _fpga_properties *) src;
	struct _fpga_properties *_dst;

	if (NULL == src || NULL == dst)
		return FPGA_INVALID_PARAM;

	if (_src->magic != FPGA_PROPERTY_MAGIC) {
		FPGA_MSG("Invalid properties object");
		return FPGA_INVALID_PARAM;
	}
	_dst = ase_malloc(sizeof(struct _fpga_properties));
	if (NULL == _dst) {
		FPGA_MSG("Failed to allocate memory for properties");
		return FPGA_NO_MEMORY;
	}

	ase_memcpy(_dst, _src, sizeof(struct _fpga_properties));

	*dst = _dst;
	return FPGA_OK;

}

fpga_result __FPGA_API__
ase_fpgaUpdateProperties(fpga_token token, fpga_properties prop)
{
	struct _fpga_token *_token = (struct _fpga_token *) token;
	struct _fpga_properties *_prop = (struct _fpga_properties *) prop;
	struct _fpga_properties _iprop;
	fpga_token_header *hdr;

	if (token == NULL)
		return FPGA_INVALID_PARAM;

	if (_prop->magic != FPGA_PROPERTY_MAGIC) {
		FPGA_MSG("Invalid properties object");
		return FPGA_INVALID_PARAM;
	}

	hdr = &_token->hdr;
	if (ASE_TOKEN_MAGIC != hdr->magic)
		return FPGA_INVALID_PARAM;

	//clear fpga_properties buffer
	ase_memset(&_iprop, 0, sizeof(struct _fpga_properties));
	_iprop.magic = FPGA_PROPERTY_MAGIC;

	if (hdr->objtype == FPGA_ACCELERATOR) {
		fpga_accelerator_state state;

		_iprop.parent = (fpga_token) token_get_parent(_token);
		if (_iprop.parent != NULL)
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_PARENT);

		if (hdr->interface == FPGA_IFC_SIM_VFIO) {
			// Only the VF has an afu_id.
			ase_memcpy(&_iprop.guid, &aseToken[2].hdr.guid, sizeof(fpga_guid));
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_GUID);
		}

		state = (session_exist_status == NOT_ESTABLISHED) ?
			FPGA_ACCELERATOR_UNASSIGNED :
			FPGA_ACCELERATOR_ASSIGNED;

		_iprop.u.accelerator.state = state;
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_ACCELERATOR_STATE);

		_iprop.u.accelerator.num_mmio = ASE_NUM_MMIO;
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_NUM_MMIO);

		_iprop.u.accelerator.num_interrupts = ASE_NUM_IRQ;
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_NUM_INTERRUPTS);

	} else {

		// Assign FME guid
		ase_memcpy(&_iprop.guid, FPGA_FME_GUID, sizeof(fpga_guid));
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_GUID);

		_iprop.u.fpga.num_slots = ASE_NUM_SLOTS;
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_NUM_SLOTS);

		_iprop.u.fpga.bbs_id = ASE_BBSID;
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_BBSID);

		_iprop.u.fpga.bbs_version.major = ASE_BBS_VERSION_MAJOR;
		_iprop.u.fpga.bbs_version.minor = ASE_BBS_VERSION_MINOR;
		_iprop.u.fpga.bbs_version.patch = ASE_BBS_VERSION_PATCH;
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_BBSVERSION);

	}

	_iprop.objtype = hdr->objtype;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_OBJTYPE);

	_iprop.segment = hdr->segment;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SEGMENT);

	_iprop.bus = hdr->bus;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_BUS);

	_iprop.device = hdr->device;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_DEVICE);

	_iprop.function = hdr->function;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_FUNCTION);

	_iprop.socket_id = ASE_SOCKET_ID;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SOCKETID);

	_iprop.vendor_id = hdr->vendor_id;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_VENDORID);

	_iprop.device_id = hdr->device_id;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_DEVICEID);

	_iprop.object_id = hdr->object_id;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_OBJECTID);

	// FPGA_PROPERTY_NUM_ERRORS

	_iprop.interface = hdr->interface;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_INTERFACE);

	_iprop.subsystem_vendor_id = hdr->subsystem_vendor_id;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SUB_VENDORID);

	_iprop.subsystem_device_id = hdr->subsystem_device_id;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SUB_DEVICEID);

	*_prop = _iprop;
	return FPGA_OK;
}

fpga_result __FPGA_API__ ase_fpgaCloneToken(fpga_token src,
					fpga_token *dst)
{
	struct _fpga_token *_src = (struct _fpga_token *)src;
	struct _fpga_token *_dst;

	if (NULL == src || NULL == dst) {
		FPGA_MSG("src or dst in NULL");
		return FPGA_INVALID_PARAM;
	}

	if (_src->hdr.magic != ASE_TOKEN_MAGIC) {
		FPGA_MSG("Invalid src");
		return FPGA_INVALID_PARAM;
	}

	_dst = ase_malloc(sizeof(struct _fpga_token));
	if (NULL == _dst) {
		FPGA_MSG("Failed to allocate memory for token");
		return FPGA_NO_MEMORY;
	}

	ase_memcpy(_dst, _src, sizeof(struct _fpga_token));
	*dst = _dst;
	return FPGA_OK;
}
