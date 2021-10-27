//
// Copyright (c) 2020, Intel Corporation
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


const char* pcie_ss_func_fmttype_to_string(uint8_t fmttype)
{
    const char *t;

    switch (fmttype)
    {
        case PCIE_FMTTYPE_MEM_READ32:   t = "MRd32 "; break;
        case PCIE_FMTTYPE_MEM_READ64:   t = "MRd64 "; break;
        case PCIE_FMTTYPE_MEM_WRITE32:  t = "MWr32 "; break;
        case PCIE_FMTTYPE_MEM_WRITE64:  t = "MWr64 "; break;
        case PCIE_FMTTYPE_CFG_WRITE:    t = "CfgWr "; break;
        case PCIE_FMTTYPE_INTR:         t = "Intr  "; break;
        case PCIE_FMTTYPE_CPL:          t = "Cpl   "; break;
        case PCIE_FMTTYPE_CPLD:         t = "CplD  "; break;
        case PCIE_FMTTYPE_SWAP32:       t = "Swap32"; break;
        case PCIE_FMTTYPE_SWAP64:       t = "Swap64"; break;
        case PCIE_FMTTYPE_CAS32:        t = "CaS32 "; break;
        case PCIE_FMTTYPE_CAS64:        t = "Cas64 "; break;
        default:                        t = "Unknown"; break;
    }

    return t;
}

static void fprintf_pcie_ss_base(FILE *stream, const t_pcie_ss_hdr_upk *hdr)
{
    fprintf(stream, "%s %s len_bytes 0x%04x",
            pcie_ss_func_fmttype_to_string(hdr->fmt_type),
            (hdr->dm_mode ? "DM" : "PU"),
            hdr->len_bytes);
}

static void fprintf_pcie_ss_mem_req(FILE *stream, const t_pcie_ss_hdr_upk *hdr)
{
    fprintf_pcie_ss_base(stream, hdr);
    if (hdr->dm_mode)
    {
        fprintf(stream, " req_id 0x%04x tag 0x%02x addr 0x%016" PRIx64,
                hdr->req_id, hdr->tag,
                hdr->u.req.addr);
    }
    else
    {
        fprintf(stream, " req_id 0x%04x tag 0x%02x lbe 0x%x fbe 0x%x addr 0x%016" PRIx64,
                hdr->req_id, hdr->tag,
                hdr->u.req.last_dw_be, hdr->u.req.first_dw_be,
                hdr->u.req.addr);
    }
}

static void fprintf_pcie_ss_cpl(FILE *stream, const t_pcie_ss_hdr_upk *hdr)
{
    fprintf_pcie_ss_base(stream, hdr);
    fprintf(stream, " cpl_id 0x%04x st %x bcm %x fc %x bytes 0x%03x req_id 0x%04x tag 0x%02x low_addr 0x%02x",
            hdr->u.cpl.comp_id, hdr->u.cpl.cpl_status, hdr->u.cpl.bcm,
            hdr->u.cpl.fc, hdr->u.cpl.byte_count,
            hdr->req_id, hdr->tag, hdr->u.cpl.low_addr);
}

static void fprintf_pcie_ss_intr(FILE *stream, const t_pcie_ss_hdr_upk *hdr)
{
    fprintf_pcie_ss_base(stream, hdr);
    fprintf(stream, " vector_num 0x%x", hdr->u.intr.vector_num);
}

void fprintf_pcie_ss_hdr(FILE *stream, const t_pcie_ss_hdr_upk *hdr)
{
    if (tlp_func_is_mem_req(hdr->fmt_type))
    {
        fprintf_pcie_ss_mem_req(stream, hdr);
    }
    else if (tlp_func_is_completion(hdr->fmt_type))
    {
        fprintf_pcie_ss_cpl(stream, hdr);
    }
    else if (tlp_func_is_interrupt_req(hdr->fmt_type))
    {
        fprintf_pcie_ss_intr(stream, hdr);
    }
    else
    {
        fprintf_pcie_ss_base(stream, hdr);
    }
}

// Print a full tdata payload if n_dwords is 0
static void fprintf_pcie_ss_bitvec(FILE *stream, const svBitVecVal *payload, int n_dwords)
{
    if (n_dwords == 0)
    {
        n_dwords = pcie_ss_param_cfg.tdata_width_bits / 32;
    }

    fprintf(stream, "0x");
    for (int i = n_dwords - 1; i >= 0; i -= 1)
    {
        uint32_t dw;
        svGetPartselBit(&dw, payload, i * 32, 32);
        if ((i & 1) && (i != n_dwords - 1)) fprintf(stream, "_");
        fprintf(stream, "%08x", dw);
    }
}

void fprintf_pcie_ss_afu_to_host(
    FILE *stream,
    long long cycle,
    bool eop,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    fprintf(stream, "afu_to_host: %lld %s %s ", cycle,
            (hdr ? "sop" : "   "), (eop ? "eop" : "   "));

    if (hdr) fprintf_pcie_ss_hdr(stream, hdr);
    fprintf(stream, " ");
    fprintf_pcie_ss_bitvec(stream, tdata, 0);
    fprintf(stream, " tkeep ");
    fprintf_pcie_ss_bitvec(stream, tkeep, pcie_ss_param_cfg.tdata_width_bits / (32 * 8));

    fprintf(stream, "\n");
    fflush(stream);
}

void fprintf_pcie_ss_host_to_afu(
    FILE *stream,
    long long cycle,
    bool eop,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    fprintf(stream, "host_to_afu: %lld %s %s ", cycle,
            (hdr ? "sop" : "   "), (eop ? "eop" : "   "));

    if (hdr) fprintf_pcie_ss_hdr(stream, hdr);
    fprintf(stream, " ");
    fprintf_pcie_ss_bitvec(stream, tdata, 0);
    fprintf(stream, " tkeep ");
    fprintf_pcie_ss_bitvec(stream, tkeep, pcie_ss_param_cfg.tdata_width_bits / (32 * 8));

    fprintf(stream, "\n");
    fflush(stream);
}
