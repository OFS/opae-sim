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
#include "pcie_tlp_stream.h"


const char* tlp_func_fmttype_to_string(uint8_t fmttype)
{
    const char *t;

    switch (fmttype)
    {
        case PCIE_FMTTYPE_MEM_READ32:   t = "MRd32"; break;
        case PCIE_FMTTYPE_MEM_READ64:   t = "MRd64"; break;
        case PCIE_FMTTYPE_MEM_WRITE32:  t = "MWr32"; break;
        case PCIE_FMTTYPE_MEM_WRITE64:  t = "MWr64"; break;
        case PCIE_FMTTYPE_CFG_WRITE:    t = "CfgWr"; break;
        case PCIE_FMTTYPE_CPL:          t = "Cpl"; break;
        case PCIE_FMTTYPE_CPLD:         t = "CplD"; break;
        case PCIE_FMTTYPE_SWAP32:       t = "Swap32"; break;
        case PCIE_FMTTYPE_SWAP64:       t = "Swap64"; break;
        case PCIE_FMTTYPE_CAS32:        t = "CaS32"; break;
        case PCIE_FMTTYPE_CAS64:        t = "Cas64"; break;
        default:                        t = "Unknown"; break;
    }

    return t;
}

static void fprintf_tlp_dw0(FILE *stream, t_tlp_hdr_dw0_upk dw0)
{
    fprintf(stream, "%s len 0x%04x [tc %d th %d td %d ep %d attr %d]",
            tlp_func_fmttype_to_string(dw0.fmttype),
            dw0.length, dw0.tc, dw0.th, dw0.td, dw0.ep, dw0.attr);
}

static void fprintf_tlp_mem_req(FILE *stream, const t_tlp_hdr_upk *hdr)
{
    fprintf_tlp_dw0(stream, hdr->dw0);
    fprintf(stream, " req_id 0x%04x tag 0x%02x lbe 0x%x fbe 0x%x addr 0x%016" PRIx64,
            hdr->u.mem.requester_id, hdr->u.mem.tag,
            hdr->u.mem.last_be, hdr->u.mem.first_be,
            hdr->u.mem.addr);
}

static void fprintf_tlp_cpl(FILE *stream, const t_tlp_hdr_upk *hdr)
{
    fprintf_tlp_dw0(stream, hdr->dw0);
    fprintf(stream, " cpl_id 0x%04x st %x bcm %x bytes 0x%03x req_id 0x%04x tag 0x%02x low_addr 0x%02x",
            hdr->u.cpl.completer_id, hdr->u.cpl.status, hdr->u.cpl.bcm, hdr->u.cpl.byte_count,
            hdr->u.cpl.requester_id, hdr->u.cpl.tag, hdr->u.cpl.lower_addr);
}

void fprintf_tlp_hdr(FILE *stream, const t_tlp_hdr_upk *hdr)
{
    if (tlp_func_is_mem_req(hdr->dw0.fmttype))
    {
        fprintf_tlp_mem_req(stream, hdr);
    }
    else if (tlp_func_is_completion(hdr->dw0.fmttype))
    {
        fprintf_tlp_cpl(stream, hdr);
    }
    else
    {
        fprintf_tlp_dw0(stream, hdr->dw0);
    }
}

// Print a full tdata payload if n_dwords is 0
static void fprintf_tlp_payload(FILE *stream, const uint32_t *payload, int n_dwords)
{
    if (n_dwords == 0)
    {
        t_ase_axis_pcie_tdata td;
        n_dwords = sizeof(td.payload) / 4;
    }

    fprintf(stream, "0x");
    for (int i = n_dwords - 1; i >= 0; i -= 1)
    {
        fprintf(stream, "%08x", payload[i]);
    }
}

void fprintf_tlp_afu_to_host(
    FILE *stream,
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
)
{
    fprintf(stream, "afu_to_host: %lld ch%d %s %s ", cycle, ch,
            (tdata->sop ? "sop" : "   "), (tdata->eop ? "eop" : "   "));

    // Interrupt? Special format if it is.
    if (tuser->afu_irq)
    {
        fprintf(stream, "irq_id %d", tdata->irq_id);
    }
    else
    {
        if (tdata->sop) fprintf_tlp_hdr(stream, hdr);
        if (!tdata->sop || tlp_func_has_data(hdr->dw0.fmttype))
        {
            fprintf(stream, " ");
            fprintf_tlp_payload(stream, tdata->payload, 0);
        }
    }

    fprintf(stream, "\n");
    fflush(stream);
}

void fprintf_tlp_host_to_afu(
    FILE *stream,
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_rx_tuser *tuser
)
{
    fprintf(stream, "host_to_afu: %lld ch%d %s %s ", cycle, ch,
            (tdata->sop ? "sop" : "   "), (tdata->eop ? "eop" : "   "));
    if (tdata->sop) fprintf_tlp_hdr(stream, hdr);
    if (!tdata->sop || tlp_func_has_data(hdr->dw0.fmttype))
    {
        fprintf(stream, " ");
        fprintf_tlp_payload(stream, tdata->payload, 0);
    }
    fprintf(stream, "\n");
    fflush(stream);
}
