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

static struct _fpga_token aseToken[3] = {
	{
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
