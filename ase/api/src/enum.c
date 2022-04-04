// Copyright(c) 2017-2021, Intel Corporation
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
#include <unistd.h>
#include "props.h"
#define ASE_FME_ID 0x3345678UL
#define BBSID 0x63000023b637277UL
#define FPGA_NUM_SLOTS 1
#define BBS_VERSION_MAJOR 6
#define BBS_VERSION_MINOR 3
#define BBS_VERSION_PATCH 0

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

struct dev_list {
	fpga_objtype objtype;
	fpga_guid guid;
	uint16_t segment;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint8_t socket_id;

	uint32_t fpga_num_slots;
	uint64_t fpga_bitstream_id;
	fpga_version fpga_bbs_version;

	fpga_accelerator_state accelerator_state;
	uint32_t accelerator_num_mmios;
	uint32_t accelerator_num_irqs;
	struct dev_list *next;
	struct dev_list *parent;
	struct dev_list *fme;
};

STATIC bool matches_filters(const fpga_properties *filter, uint32_t num_filter,
		fpga_token *token, uint64_t *j)
{
	uint32_t i;
	if (filter == NULL)
		return true;
	struct _fpga_properties *_filter = (struct _fpga_properties *)*filter;
	struct _fpga_token *_tok = (struct _fpga_token *)*token;
	if (!num_filter)	// no filter == match everything
		return true;

	if (_filter->valid_fields == 0)
		return true;

	if (FIELD_VALID(_filter, FPGA_PROPERTY_DEVICE)) {
		return true;

	}

	if (FIELD_VALID(_filter, FPGA_PROPERTY_FUNCTION)) {
		return true;
	}

	for (i = 0; i < num_filter; ++i) {
		if (FIELD_VALID(_filter, FPGA_PROPERTY_PARENT)) {
			if (_filter->parent == NULL)
				return false;

			if (((struct _fpga_token *)_filter->parent)->hdr.objtype == FPGA_ACCELERATOR)
				return false;
			else
				*j = 1;
		}
		if (FIELD_VALID(_filter, FPGA_PROPERTY_OBJECTID)) {
			uint64_t objid;
			fpga_result result;
			result = objectid_for_ase(&objid);
			if (result != FPGA_OK || _filter->object_id != objid) {
				return false;
			}
		}
		if (_filter->objtype != _tok->hdr.objtype)
			return false;

		if (FIELD_VALID(_filter, FPGA_PROPERTY_GUID)) {
			if (0 != memcmp(_tok->hdr.guid, _filter->guid,
					sizeof(fpga_guid))) {
				BEGIN_RED_FONTCOLOR;
				printf("  [APP]  Filter mismatch\n");
				END_RED_FONTCOLOR;
				return false;
			}
		}
		_filter++;

	}
	return true;
}


fpga_result __FPGA_API__
ase_fpgaEnumerate(const fpga_properties *filters, uint32_t num_filters,
	      fpga_token *tokens, uint32_t max_tokens,
	      uint32_t *num_matches)
{
	uint64_t i;
	fpga_token ase_token[2];
	aseToken[0].hdr.objtype = FPGA_DEVICE;
	aseToken[1].hdr.objtype = FPGA_ACCELERATOR;

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

	uint64_t afuid_data[2];
	fpga_guid readback_afuid;

	if (session_exist_status == NOT_ESTABLISHED) {
		session_init();
		ase_memcpy(&aseToken[0].hdr.guid, FPGA_FME_GUID, sizeof(fpga_guid));

		mmio_read64(0x8, &afuid_data[0]);
		mmio_read64(0x10, &afuid_data[1]);
		// Convert afuid_data to readback_afuid
		// e.g.: readback{0x5037b187e5614ca2, 0xad5bd6c7816273c2} -> "5037B187-E561-4CA2-AD5B-D6C7816273C2"
		api_guid_to_fpga(afuid_data[1], afuid_data[0], readback_afuid);
		ase_memcpy(&aseToken[1].hdr.guid, readback_afuid, sizeof(fpga_guid));
	}

	for (i = 0; i < 2; i++)
		ase_token[i] = &aseToken[i];
	*num_matches = 0;
	for (i = 0; i < 2; i++) {
		if (matches_filters(filters, num_filters, &ase_token[i], &i)) {
			if (*num_matches < max_tokens)	{
				if (FPGA_OK != ase_fpgaCloneToken(ase_token[i], &tokens[*num_matches]))
					FPGA_MSG("Error cloning token");
			}
			++*num_matches;
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
	fpga_result result;
	struct _fpga_properties _iprop;

	if (token == NULL)
		return FPGA_INVALID_PARAM;

	if (_prop->magic != FPGA_PROPERTY_MAGIC) {
		FPGA_MSG("Invalid properties object");
		return FPGA_INVALID_PARAM;
	}

	if (ASE_TOKEN_MAGIC != _token->hdr.magic)
		return FPGA_INVALID_PARAM;
	//clear fpga_properties buffer
	ase_memset(&_iprop, 0, sizeof(struct _fpga_properties));
	_iprop.magic = FPGA_PROPERTY_MAGIC;
	result = objectid_for_ase(&_iprop.object_id);
	if (result == 0)
		SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_OBJECTID);
	if (ASE_TOKEN_MAGIC == _token->hdr.magic) {
		// The input token is either an FME or an AFU.
		if (memcmp(_token->hdr.guid, FPGA_FME_GUID, sizeof(fpga_guid)) == 0) {
			_iprop.objtype = FPGA_DEVICE;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_OBJTYPE);
			_iprop.device_id = ASE_ID;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_DEVICEID);
			//Assign FME guid
			ase_memcpy(&_iprop.guid, FPGA_FME_GUID, sizeof(fpga_guid));
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_GUID);
			_iprop.u.fpga.num_slots = ASE_NUM_SLOTS;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_NUM_SLOTS);
			_iprop.u.fpga.bbs_id = ASE_ID;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_BBSID);
			_iprop.u.fpga.bbs_version.major = 0;
			_iprop.u.fpga.bbs_version.minor = 0;
			_iprop.u.fpga.bbs_version.patch = 0;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_BBSVERSION);
		} else {
			ase_memcpy(&_iprop.guid, &aseToken[1].hdr.guid, sizeof(fpga_guid));
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_GUID);
			_iprop.u.accelerator.state = FPGA_ACCELERATOR_ASSIGNED;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_ACCELERATOR_STATE);
			_iprop.parent = (fpga_token) token_get_parent(_token);
			if (_iprop.parent != NULL)
				SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_PARENT);
			_iprop.objtype = FPGA_ACCELERATOR;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_OBJTYPE);
			_iprop.u.accelerator.num_mmio = 2;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_NUM_MMIO);
			_iprop.u.accelerator.num_interrupts = 0;
			SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_NUM_INTERRUPTS);

		}
	}
	_iprop.vendor_id = 0x8086;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_VENDORID);

	_iprop.device_id = ASE_ID;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_DEVICEID);

	_iprop.subsystem_vendor_id = 0x8086;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SUB_VENDORID);

	_iprop.subsystem_device_id = ASE_ID;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SUB_DEVICEID);

	_iprop.bus = ASE_BUS;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_BUS);

	_iprop.device = ASE_DEVICE;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_DEVICE);

	_iprop.function = ASE_FUNCTION;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_FUNCTION);

	_iprop.socket_id = ASE_SOCKET_ID;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_SOCKETID);

	_iprop.interface = FPGA_IFC_SIM;
	SET_FIELD_VALID(&_iprop, FPGA_PROPERTY_INTERFACE);

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

fpga_result objectid_for_ase(uint64_t *object_id)
{
	*object_id = ASE_OBJID;

	return FPGA_OK;
}
