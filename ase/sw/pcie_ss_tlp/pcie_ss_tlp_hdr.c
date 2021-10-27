//
// Copyright (c) 2021, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// Neither the name of the Intel Corporation nor the names of its contributors
// may be used to endorse or promote products derived from this software
// without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "ase_common.h"
#include "pcie_ss_tlp_stream.h"


// ========================================================================
//
//  Convert between C and DPI-C data structures
//
// ========================================================================

void pcie_ss_tlp_hdr_reset(t_pcie_ss_hdr_upk *hdr)
{
    memset(hdr, 0, sizeof(*hdr));
}

void pcie_ss_tlp_payload_reset(
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
)
{
    memset(tdata, 0, pcie_ss_param_cfg.tdata_width_bits / 8);
    memset(tkeep, 0, pcie_ss_param_cfg.tdata_width_bits / 64);

    svPutPartselBit(tuser, 0, 0, pcie_ss_param_cfg.tuser_width_bits);
}

// Pack the expanded C TLP message into the encoded packed vector
void pcie_ss_tlp_hdr_pack(
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep,
    const t_pcie_ss_hdr_upk *hdr
)
{
    pcie_ss_tlp_payload_reset(tdata, tuser, tkeep);

    // Bit 0 of tuser indicates data mover mode
    svPutPartselBit(tuser, hdr->dm_mode, 0, 1);

    // Set keep mask for header
    svPutPartselBit(tkeep, ~0, 0, pcie_ss_cfg.tlp_hdr_dwords * 4);

    // Common header components

    uint32_t v = 0;
    v |= (uint32_t)(hdr->fmt_type) << 24;
    v |= ((uint32_t)(hdr->tag >> 9) & 1) << 23;
    v |= ((uint32_t)(hdr->tag >> 8) & 1) << 19;
    v |= (uint32_t)((hdr->len_bytes >> 2) & 0x3ff);
    svPutPartselBit(tdata, v, 0, 32);

    v = 0;
    v |= (uint32_t)(hdr->bar_number) << 25;
    v |= (uint32_t)(hdr->mm_mode) << 24;
    v |= (uint32_t)(hdr->slot_num) << 15;
    v |= (uint32_t)(hdr->vf_active) << 14;
    v |= (uint32_t)(hdr->vf_num) << 3;
    v |= (uint32_t)(hdr->pf_num);
    svPutPartselBit(tdata, v, 32*5, 32);

    svPutPartselBit(tdata, hdr->metadata, 32*7, 32);
    svPutPartselBit(tdata, hdr->metadata >> 32, 32*6, 32);

    if (tlp_func_is_mem_req(hdr->fmt_type))
    {
        v = 0;
        v |= (uint32_t)(hdr->req_id) << 16;
        v |= (uint32_t)(hdr->tag & 0xff) << 8;
        v |= (uint32_t)(hdr->u.req.last_dw_be) << 4;
        v |= (uint32_t)(hdr->u.req.first_dw_be);
        svPutPartselBit(tdata, v, 32*1, 32);

        // Unpacked address is always a 64 bit value
        if (tlp_func_is_addr64(hdr->fmt_type))
        {
            svPutPartselBit(tdata, hdr->u.req.addr >> 32, 32*2, 32);
            svPutPartselBit(tdata, hdr->u.req.addr >> 2, 32*3 + 2, 30);
        }
        else
        {
            svPutPartselBit(tdata, hdr->u.req.addr, 32*2, 32);
        }
    }
    else if (tlp_func_is_completion(hdr->fmt_type))
    {
        if (hdr->dm_mode)
        {
            v = 0;
            v |= (uint32_t)(hdr->tag) << 22;
            v |= (uint32_t)(hdr->u.cpl.fc & 1) << 21;
            v |= (uint32_t)((hdr->len_bytes >> 12) & 3) << 18;
            v |= (uint32_t)(hdr->len_bytes & 3) << 16;
            v |= (uint32_t)(hdr->u.cpl.low_addr >> 8) & 0xffff;
            svPutPartselBit(tdata, v, 32*3, 32);

            svPutPartselBit(tdata, hdr->u.cpl.low_addr, 32*2, 8);

            v = 0;
            v |= (uint32_t)(hdr->u.cpl.cpl_status) << 13;
            svPutPartselBit(tdata, v, 32*1, 32);
        }
        else
        {
            v = 0;
            v |= (uint32_t)(hdr->req_id) << 16;
            v |= (uint32_t)(hdr->tag & 0xff) << 8;
            v |= (uint32_t)(hdr->u.cpl.low_addr & 0x7f);
            svPutPartselBit(tdata, v, 32*2, 32);

            v = 0;
            v |= (uint32_t)(hdr->u.cpl.comp_id) << 16;
            v |= (uint32_t)(hdr->u.cpl.cpl_status) << 13;
            v |= (uint32_t)(hdr->u.cpl.bcm) << 12;
            v |= (uint32_t)(hdr->u.cpl.byte_count);
            svPutPartselBit(tdata, v, 32*1, 32);
        }
    }
}

// Unpack the hardware format into a C TLP struct
void pcie_ss_tlp_hdr_unpack(
    t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    pcie_ss_tlp_hdr_reset(hdr);

    // Bit 0 of tuser indicates data mover mode
    hdr->dm_mode = svGetBitselBit(tuser, 0);

    // Common header components

    uint32_t dw0;
    svGetPartselBit(&dw0, tdata, 0, 32);
    hdr->fmt_type = (dw0 >> 24) & 0xff;

    uint32_t v, v1, v2;
    svGetPartselBit(&v, tdata, 32*5, 32);
    hdr->bar_number = (v >> 25) & 0x7f;
    hdr->mm_mode = (v >> 24) & 1;
    hdr->slot_num = (v >> 15) & 0x1f;
    hdr->vf_active = (v >> 14) & 1;
    hdr->vf_num = (v >> 3) & 0x7ff;
    hdr->pf_num = v & 0x7;

    svGetPartselBit(&v, tdata, 32*7, 32);
    svGetPartselBit(&v1, tdata, 32*6, 32);
    hdr->metadata = ((uint64_t)v1 << 32) | v;

    if (tlp_func_is_mem_req(hdr->fmt_type))
    {
        svGetPartselBit(&v, tdata, 32*1, 32);
        hdr->tag = (((dw0 >> 23) & 1) << 9) |    // tag_h
                   (((dw0 >> 19) & 1) << 8) |    // tag_m
                   ((v >> 8) & 0xff);            // tag_l

        if (hdr->dm_mode)
        {
            hdr->len_bytes = (((v1 >> 18) & 0xfff) << 12) | // length_h
                             ((dw0 & 0x3ff) << 2) |         // length_m
                             ((v1 >> 16) & 3);              // length_l

            svGetPartselBit(&v1, tdata, 32*2, 32);
            svGetPartselBit(&v2, tdata, 32*3, 32);
            hdr->u.req.addr = ((uint64_t)v1 << 32) |   // host_addr_h
                              (v2 & ~3) |              // host_addr_m
                              ((v >> 30) & 3);         // host_addr_l

            // DM doesn't have a req_id. Compute one from PF/VF.
            hdr->req_id = (hdr->vf_num << 4) |
                          (hdr->vf_active << 3) |
                          (hdr->pf_num);

            // Byte enable not used (DM addresses/sizes are bytes)
            hdr->u.req.last_dw_be = 0xf;
            hdr->u.req.first_dw_be = 0xf;
        }
        else
        {
            hdr->len_bytes = (dw0 & 0x3ff) << 2;

            hdr->req_id = (v >> 16) & 0xffff;
            hdr->u.req.last_dw_be = (v >> 4) & 0xf;
            hdr->u.req.first_dw_be = v & 0xf;

            if (tlp_func_is_addr64(hdr->fmt_type))
            {
                svGetPartselBit(&v, tdata, 32*3, 32);
                svGetPartselBit(&v1, tdata, 32*2, 32);
                hdr->u.req.addr = ((uint64_t)v1 << 32) | (v & ~3);
            }
            else
            {
                svGetPartselBit(&v, tdata, 32*2, 32);
                hdr->u.req.addr = v;
            }
        }
    }
    else if (tlp_func_is_completion(hdr->fmt_type))
    {
        if (hdr->dm_mode)
        {
            ASE_ERR("DM (data mover) mode completions not yet supported\n");
            start_simkill_countdown();
        }

        hdr->len_bytes = (dw0 & 0x3ff) << 2;

        svGetPartselBit(&v, tdata, 32*2, 32);
        hdr->req_id = (v >> 16) & 0xffff;
        hdr->tag = (((dw0 >> 23) & 1) << 9) |    // tag_h
                   (((dw0 >> 19) & 1) << 8) |    // tag_m
                   ((v >> 8) & 0xff);            // tag_l
        hdr->u.cpl.low_addr = v & 0x7f;

        svGetPartselBit(&v, tdata, 32*1, 32);
        hdr->u.cpl.comp_id = (v >> 16) & 0xffff;
        hdr->u.cpl.cpl_status = (v >> 13) & 0x7;
        hdr->u.cpl.bcm = (v >> 12) & 1;
        hdr->u.cpl.byte_count = v & 0xfff;
    }
    else if (tlp_func_is_interrupt_req(hdr->fmt_type))
    {
        if (! hdr->dm_mode)
        {
            ASE_ERR("Interrupts must be DM (data mover) mode\n");
            start_simkill_countdown();
        }

        svGetPartselBit(&v, tdata, 32*2, 16);
        hdr->u.intr.vector_num = v;
    }
}
