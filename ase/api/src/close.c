// Copyright(c) 2017, Intel Corporation
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
#endif

#include <opae/access.h>
#include "common_int.h"
#include "token.h"
#include <ase_common.h>
#include "ase_host_memory.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

fpga_result __FPGA_API__ ase_fpgaClose(fpga_handle handle)
{
	fpga_result result;
	if (NULL == handle) {
		FPGA_MSG("Handle is NULL");
		return FPGA_INVALID_PARAM;
	}

	struct _fpga_handle *_handle = (struct _fpga_handle *) handle;

	// Track open AFUs
	struct _fpga_token *_token = (struct _fpga_token *) _handle->token;
	ase_open_afus_by_tok_idx &= ~(UINT64_C(1) << _token->idx);

	// ASE Release
	if (!ase_open_afus_by_tok_idx)
		session_deinit();

	ase_host_memory_terminate_afu(_handle->afu_idx);
	wsid_tracker_cleanup(_handle->wsid_root, NULL);

	free(_handle);
	_handle = NULL;
	result = FPGA_OK;
	return result;
}
