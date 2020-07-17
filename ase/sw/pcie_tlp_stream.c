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

#include <assert.h>

#include "ase_common.h"
#include "ase_host_memory.h"
#include "pcie_tlp_stream.h"

static FILE *logfile;

static t_ase_axis_param_cfg param_cfg;
static bool in_reset;
static uint64_t cur_cycle;

// ========================================================================
//
//  Convert between C and DPI-C data structures
//
// ========================================================================

static void tlp_hdr_dw0_reset(t_tlp_hdr_dw0_upk *dw0)
{
    memset(dw0, 0, sizeof(*dw0));
}

// Pack the C dw0 struct into the expected 32 bit object
static uint32_t tlp_hdr_dw0_pack(const t_tlp_hdr_dw0_upk *dw0)
{
    uint32_t p = 0;
    p |= (uint32_t)(dw0->fmttype) << 24;
    p |= (uint32_t)(dw0->tc) << 20;
    p |= (uint32_t)(dw0->th) << 16;
    p |= (uint32_t)(dw0->td) << 15;
    p |= (uint32_t)(dw0->ep) << 14;
    p |= (uint32_t)(dw0->attr) << 12;
    p |= dw0->length;
    return p;
}

// Unpack the 32 bit dw0 into the C dw0 struct
static void tlp_hdr_dw0_unpack(t_tlp_hdr_dw0_upk *dw0, uint32_t p)
{
    tlp_hdr_dw0_reset(dw0);
    dw0->fmttype = (p >> 24) & 0x7f;
    dw0->tc = (p >> 20) & 7;
    dw0->th = (p >> 16) & 1;
    dw0->td = (p >> 15) & 1;
    dw0->ep = (p >> 14) & 1;
    dw0->attr = (p >> 12) & 3;
    dw0->length = p & 0x3ff;
}

static void tlp_hdr_reset(t_tlp_hdr_upk *hdr)
{
    memset(hdr, 0, sizeof(*hdr));
}

// Pack the C TLP message into the expected packed vector
static void tlp_hdr_pack(svBitVecVal *hdr, const t_tlp_hdr_upk *tlp_upk)
{
    svPutPartselBit(hdr, tlp_hdr_dw0_pack(&tlp_upk->dw0), 32*3, 32);

    if (tlp_func_is_mem_req(tlp_upk->dw0.fmttype))
    {
        uint32_t v = 0;
        v |= (uint32_t)(tlp_upk->u.mem.requester_id) << 16;
        v |= (uint32_t)(tlp_upk->u.mem.tag) << 8;
        v |= (uint32_t)(tlp_upk->u.mem.last_be) << 4;
        v |= tlp_upk->u.mem.first_be;
        svPutPartselBit(hdr, v, 32*2, 32);

        // Unpacked address is always a 64 bit value
        if (tlp_func_is_addr64(tlp_upk->dw0.fmttype))
        {
            svPutPartselBit(hdr, tlp_upk->u.mem.addr >> 32, 32, 32);
            svPutPartselBit(hdr, tlp_upk->u.mem.addr, 0, 32);
        }
        else
        {
            svPutPartselBit(hdr, tlp_upk->u.mem.addr, 32, 32);
            svPutPartselBit(hdr, 0, 0, 32);
        }
    }
    else if (tlp_func_is_completion(tlp_upk->dw0.fmttype))
    {
        uint32_t v;

        v = 0;
        v |= (uint32_t)(tlp_upk->u.cpl.completer_id) << 16;
        v |= (uint32_t)(tlp_upk->u.cpl.status) << 13;
        v |= (uint32_t)(tlp_upk->u.cpl.bcm) << 12;
        v |= tlp_upk->u.cpl.byte_count;
        svPutPartselBit(hdr, v, 32*2, 32);

        v = 0;
        v |= (uint32_t)(tlp_upk->u.cpl.requester_id) << 16;
        v |= (uint32_t)(tlp_upk->u.cpl.tag) << 8;
        v |= tlp_upk->u.cpl.lower_addr;
        svPutPartselBit(hdr, v, 32, 32);

        svPutPartselBit(hdr, 0, 0, 32);
    }
}

// Unpack the hardware format into a C TLP message
static void tlp_hdr_unpack(t_tlp_hdr_upk *tlp_upk, const svBitVecVal *hdr)
{
    uint32_t dw0;
    svGetPartselBit(&dw0, hdr, 32*3, 32);
    tlp_hdr_dw0_unpack(&tlp_upk->dw0, dw0);

    if (tlp_func_is_mem_req(tlp_upk->dw0.fmttype))
    {
        uint32_t v;

        svGetPartselBit(&v, hdr, 32*2, 32);
        tlp_upk->u.mem.requester_id = v >> 16;
        tlp_upk->u.mem.tag = v >> 8;
        tlp_upk->u.mem.last_be = (v >> 4) & 0xf;
        tlp_upk->u.mem.first_be = v & 0xf;

        // Unpacked address is always a 64 bit value
        uint32_t addr;
        svGetPartselBit(&addr, hdr, 32, 32);
        if (tlp_func_is_addr64(tlp_upk->dw0.fmttype))
        {
            svGetPartselBit(&v, hdr, 0, 32);
            tlp_upk->u.mem.addr = ((uint64_t)addr << 32) | v;
        }
        else
        {
            tlp_upk->u.mem.addr = addr;
        }
    }
    else if (tlp_func_is_completion(tlp_upk->dw0.fmttype))
    {
        uint32_t v;

        svGetPartselBit(&v, hdr, 32*2, 32);
        tlp_upk->u.cpl.completer_id = v >> 16;
        tlp_upk->u.cpl.status = (v >> 13) & 7;
        tlp_upk->u.cpl.bcm = (v >> 12) & 1;
        tlp_upk->u.cpl.byte_count = v & 0xfff;

        svGetPartselBit(&v, hdr, 32, 32);
        tlp_upk->u.cpl.requester_id = v >> 16;
        tlp_upk->u.cpl.tag = v >> 8;
        tlp_upk->u.cpl.lower_addr = v & 0x7f;
    }
}


// ========================================================================
//
//  Utilities
//
// ========================================================================

static unsigned long next_rand = 1;

// Local repeatable random number generator
int pcie_tlp_rand(void)
{
    next_rand = next_rand * 1103515245 + 12345;
    return ((unsigned)(next_rand/65536) % 32768);
}


// ========================================================================
//
//  MMIO state
//
// ========================================================================

//
// Record state of in-flight MMIO reads.
//

typedef struct
{
    uint64_t start_cycle;
    uint16_t tid;
    bool busy;
} t_mmio_read_state;

// Vector of MMIO read states, indexed by what must be unique PCIe
// transaction IDs. The number of IDs is set by pcie_param_init(), which
// allocates the vector.
static t_mmio_read_state *mmio_read_state;

// Number of DWORDs remaining in current host to AFU mmio request
static uint32_t mmio_req_dw_rem;

// Cycle of last MMIO request
static uint64_t last_mmio_req_cycle;


// ========================================================================
//
//  DMA state
//
// ========================================================================

//
// Record state of in-flight MMIO reads.
//

typedef struct dma_read_state
{
    struct dma_read_state *next;
    uint64_t start_cycle;
    t_tlp_hdr_upk req_hdr;
    bool busy;
} t_dma_read_state;

static t_dma_read_state *dma_read_rsp_head;
static t_dma_read_state *dma_read_rsp_tail;

// Number of DWORDs remaining in current host to AFU read completion
static uint32_t dma_read_rsp_dw_rem;

// Vector of DMA read states, indexed by what must be unique PCIe
// transaction IDs. The number of IDs is set by pcie_param_init(), which
// allocates the vector.
static t_dma_read_state *dma_read_state;

static uint32_t num_dma_reads_pending;
static uint32_t num_dma_writes_pending;

// Buffer space, indexed by tag, for holding read responses.
static uint32_t **read_rsp_data;
static uint32_t read_rsp_n_entries;


// ========================================================================
//
//  AFU to host processing
//
// ========================================================================

static void pcie_tlp_a2h_error_and_kill(
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
)
{
    BEGIN_RED_FONTCOLOR;
    fprintf_tlp_afu_to_host(stdout, cycle, ch, hdr, tdata, tuser);
    END_RED_FONTCOLOR;
    start_simkill_countdown();
}


//
// Process a completion with data flit. Proper SOP placement has already
// been checked.
//
static void pcie_tlp_a2h_cpld(
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *new_hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
)
{
    static t_tlp_hdr_upk hdr;
    static uint32_t next_dw_idx;
    static uint32_t *payload = NULL;

    if (payload == NULL)
    {
        // Allocate a payload buffer. Initialization should have happened before
        // the first flit arrived, so the configuration state is known.
        assert(param_cfg.max_payload_bytes);
        payload = ase_malloc(param_cfg.max_payload_bytes);
    }

    if (tdata->sop)
    {
        memcpy(&hdr, new_hdr, sizeof(hdr));
        next_dw_idx = 0;

        if (hdr.u.cpl.tag > param_cfg.max_outstanding_mmio_rd_reqs)
        {
            ASE_ERR("AFU Tx TLP - Illegal MMIO read response tag:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
        if ((hdr.dw0.length * 4) != hdr.u.cpl.byte_count)
        {
            ASE_ERR("AFU Tx TLP - Split MMIO completion not supported:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
        if ((hdr.dw0.length * 4) > param_cfg.max_payload_bytes)
        {
            ASE_ERR("AFU Tx TLP - MMIO completion larger than max payload bytes (%d):\n",
                    param_cfg.max_payload_bytes);
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
        if (hdr.u.cpl.byte_count > 64)
        {
            ASE_ERR("AFU Tx TLP - MMIO completion larger than 64 bytes not supported:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
    }

    // How many DWORDs (uint32_t) are still expected?
    uint32_t payload_dws = hdr.dw0.length - next_dw_idx;
    if (payload_dws > (param_cfg.channel_payload_bytes / 4))
    {
        // This is not the last flit in the packet.
        if (tdata->eop)
        {
            ASE_ERR("AFU Tx TLP - premature end of MMIO completion:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }

        payload_dws = param_cfg.channel_payload_bytes / 4;
    }
    else
    {
        // This should be the last flit in the packet.
        if (!tdata->eop)
        {
            ASE_ERR("AFU Tx TLP - expected EOP in MMIO completion:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
    }

    // Copy payload data
    for (int i = 0; i < payload_dws; i += 1)
    {
        svGetPartselBit(&payload[next_dw_idx + i], tdata->payload, i * 32, 32);
    }
    next_dw_idx += payload_dws;

    // Packet complete?
    if (tdata->eop)
    {
        if (!mmio_read_state[hdr.u.cpl.tag].busy)
        {
            ASE_ERR("AFU Tx TLP - MMIO read response tag is not active:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }

        static mmio_t mmio_pkt;
        mmio_pkt.tid = mmio_read_state[hdr.u.cpl.tag].tid;
        mmio_pkt.write_en = MMIO_READ_REQ;
        mmio_pkt.width = hdr.dw0.length * 32;
        mmio_pkt.addr = hdr.u.cpl.lower_addr;
        mmio_pkt.resp_en = 1;
        mmio_pkt.slot_idx = hdr.u.cpl.tag;
        memcpy(mmio_pkt.qword, payload, hdr.dw0.length * 4);
        mmio_read_state[hdr.u.cpl.tag].busy = false;
        
        mmio_response(&mmio_pkt);
    }
}


//
// Process a DMA write request. Proper SOP placement has already
// been checked.
//
static void pcie_tlp_a2h_mwr(
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *new_hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
)
{
    static t_tlp_hdr_upk hdr;
    static uint32_t next_dw_idx;
    static uint32_t *payload = NULL;

    if (payload == NULL)
    {
        // Allocate a payload buffer. Initialization should have happened before
        // the first flit arrived, so the configuration state is known.
        assert(param_cfg.max_payload_bytes);
        payload = ase_malloc(param_cfg.max_payload_bytes);
    }

    if (tdata->sop)
    {
        memcpy(&hdr, new_hdr, sizeof(hdr));
        next_dw_idx = 0;

        if ((hdr.dw0.length * 4) > param_cfg.max_payload_bytes)
        {
            ASE_ERR("AFU Tx TLP - DMA write larger than max payload bytes (%d):\n",
                    param_cfg.max_payload_bytes);
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
        if ((hdr.dw0.length == 1) && (hdr.u.mem.last_be != 0))
        {
            ASE_ERR("AFU Tx TLP - DMA write last_be must be 0 on single DWORD writes:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
    }

    // How many DWORDs (uint32_t) are still expected?
    uint32_t payload_dws = hdr.dw0.length - next_dw_idx;
    if (payload_dws > (param_cfg.channel_payload_bytes / 4))
    {
        // This is not the last flit in the packet.
        if (tdata->eop)
        {
            ASE_ERR("AFU Tx TLP - premature end of DMA write:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }

        payload_dws = param_cfg.channel_payload_bytes / 4;
    }
    else
    {
        // This should be the last flit in the packet.
        if (!tdata->eop)
        {
            ASE_ERR("AFU Tx TLP - expected EOP in DMA write:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
        }
    }

    // Copy payload data
    for (int i = 0; i < payload_dws; i += 1)
    {
        svGetPartselBit(&payload[next_dw_idx + i], tdata->payload, i * 32, 32);
    }
    next_dw_idx += payload_dws;

    // Packet complete?
    if (tdata->eop)
    {
        // Write to memory
        ase_host_memory_write_req wr_req;
        wr_req.req = HOST_MEM_REQ_WRITE;
        wr_req.data_bytes = hdr.dw0.length * 4;
        wr_req.addr = hdr.u.mem.addr;
        if ((hdr.u.mem.first_be != 0xf) || ((hdr.dw0.length > 1) && (hdr.u.mem.last_be != 0xf)))
        {
            wr_req.byte_en = 1;
            wr_req.first_be = hdr.u.mem.first_be;
            wr_req.last_be = hdr.u.mem.last_be;
        }
        else
        {
            wr_req.byte_en = 0;
            wr_req.first_be = 0;
            wr_req.last_be = 0;
        }

        mqueue_send(sim2app_membus_wr_req_tx, (char *) &wr_req, sizeof(wr_req));
        mqueue_send(sim2app_membus_wr_req_tx, (char *) payload, wr_req.data_bytes);

        // Update count of pending write responses
        num_dma_writes_pending += 1;
    }
}


//
// Process a DMA read request.
//
static void pcie_tlp_a2h_mrd(
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
)
{
    if (!tdata->eop)
    {
        ASE_ERR("AFU Tx TLP - expected EOP with DMA read request:\n");
        pcie_tlp_a2h_error_and_kill(cycle, ch, hdr, tdata, tuser);
    }

    if ((hdr->dw0.length * 4) > param_cfg.max_payload_bytes)
    {
        ASE_ERR("AFU Tx TLP - DMA read larger than max payload bytes (%d):\n",
                param_cfg.max_payload_bytes);
        pcie_tlp_a2h_error_and_kill(cycle, ch, hdr, tdata, tuser);
    }

    if (hdr->u.mem.tag >= param_cfg.max_outstanding_dma_rd_reqs)
    {
        ASE_ERR("AFU Tx TLP - Illegal DMA read request tag:\n");
        pcie_tlp_a2h_error_and_kill(cycle, ch, hdr, tdata, tuser);
    }

    uint32_t tag = hdr->u.mem.tag;
    if (dma_read_state[tag].busy)
    {
        ASE_ERR("AFU Tx TLP - DMA read request tag already in use:\n");
        pcie_tlp_a2h_error_and_kill(cycle, ch, hdr, tdata, tuser);
    }

    // Record read request
    dma_read_state[tag].busy = true;
    dma_read_state[tag].next = NULL;
    dma_read_state[tag].start_cycle = cycle;
    memcpy(&dma_read_state[tag].req_hdr, hdr, sizeof(t_tlp_hdr_upk));

    ase_host_memory_read_req rd_req;
    rd_req.req = HOST_MEM_REQ_READ;
    rd_req.addr = hdr->u.mem.addr;
    if ((hdr->dw0.length == 1) && ! hdr->u.mem.last_be && ! hdr->u.mem.first_be)
    {
        // Single word read with all bytes disabled -- a fence
        rd_req.data_bytes = 0;
    }
    else
    {
        rd_req.data_bytes = hdr->dw0.length * 4;
    }
    rd_req.tag = hdr->u.mem.tag;
    mqueue_send(sim2app_membus_rd_req_tx, (char *)&rd_req, sizeof(rd_req));

    // Update count of pending read responses
    num_dma_reads_pending += 1;
}


//
// Complete a DMA write by receiving a response from the remote memory
// model. The response indicates whether the address was legal and
// the write was successful. No response is sent to the FPGA.
//
static void pcie_complete_dma_writes()
{
    while (num_dma_writes_pending)
    {
        ase_host_memory_write_rsp wr_rsp;
        int status;

        // The only task required here is to consume the response from the
        // application. The response indicates whether the address was
        // valid.  Raise an error for invalid addresses.

        status = mqueue_recv(app2sim_membus_wr_rsp_rx, (char *) &wr_rsp, sizeof(wr_rsp));

        if (status == ASE_MSG_PRESENT)
        {
            if (wr_rsp.status != HOST_MEM_STATUS_VALID)
            {
                memline_addr_error("WRITE", wr_rsp.status, wr_rsp.pa, wr_rsp.va);
                break;
            }

            num_dma_writes_pending -= 1;
        }
        else
        {
            break;
        }
    }
}

static void pcie_receive_dma_reads()
{
    while (num_dma_reads_pending)
    {
        ase_host_memory_read_rsp rd_rsp;
        int status;

        // Receive memory read responses from the memory model and queue them
        // to be sent to the PCIe hardware receiver.

        status = mqueue_recv(app2sim_membus_rd_rsp_rx, (char *) &rd_rsp, sizeof(rd_rsp));

        if (status == ASE_MSG_PRESENT)
        {
            if (rd_rsp.status != HOST_MEM_STATUS_VALID)
            {
                memline_addr_error("READ", rd_rsp.status, rd_rsp.pa, rd_rsp.va);
                break;
            }

            // Get the data, which was sent separately
            if (rd_rsp.data_bytes) {
                uint32_t *buf = read_rsp_data[rd_rsp.tag];
                while ((status = mqueue_recv(app2sim_membus_rd_rsp_rx, (char *)buf, rd_rsp.data_bytes)) != ASE_MSG_PRESENT) {
                    if (status == ASE_MSG_ERROR) break;
                }
            }

            num_dma_reads_pending -= 1;

            // Push the read on the list of pending PCIe completions
            uint32_t tag = rd_rsp.tag;
            if (dma_read_rsp_tail == NULL)
            {
                dma_read_rsp_head = &dma_read_state[tag];
            }
            else
            {
                dma_read_rsp_tail->next = &dma_read_state[tag];
            }
            dma_read_rsp_tail = &dma_read_state[tag];
        }
        else
        {
            break;
        }
    }
}


// ========================================================================
//
//  Host to AFU processing
//
// ========================================================================

// Linked list of pending MMIO requests
typedef struct mmio_list
{
    mmio_t mmio_pkt;
    struct mmio_list *next;
} t_mmio_list;

static t_mmio_list *mmio_req_head;
static t_mmio_list *mmio_req_tail;

//
// Push a new MMIO request on the processing list.
//
void pcie_mmio_new_req(const mmio_t *pkt)
{
    // Allocate a request
    t_mmio_list *m = ase_malloc(sizeof(t_mmio_list));
    memcpy(&m->mmio_pkt, pkt, sizeof(mmio_t));

    // Push it on the tail of the list
    m->next = NULL;
    if (mmio_req_head == NULL)
    {
        mmio_req_head = m;
    }
    else
    {
        mmio_req_tail->next = m;
    }
    mmio_req_tail = m;

    // Track reads
    if (m->mmio_pkt.write_en == MMIO_READ_REQ)
    {
        // Initialization must be complete by now
        assert(mmio_read_state != NULL);

        if (m->mmio_pkt.slot_idx >= param_cfg.max_outstanding_mmio_rd_reqs)
        {
            ASE_ERR("MMIO read request slot IDX (%d) exceeds max MMIO read IDX (%d)\n",
                    m->mmio_pkt.slot_idx, param_cfg.max_outstanding_mmio_rd_reqs);
            start_simkill_countdown();
        }
        if (mmio_read_state[m->mmio_pkt.slot_idx].busy)
        {
            ASE_ERR("MMIO read request slot IDX (%d) already busy\n",
                    m->mmio_pkt.slot_idx);
            start_simkill_countdown();
        }

        mmio_read_state[m->mmio_pkt.slot_idx].busy = true;
        mmio_read_state[m->mmio_pkt.slot_idx].start_cycle = cur_cycle;
        mmio_read_state[m->mmio_pkt.slot_idx].tid = m->mmio_pkt.tid;
    }
}


//
// Process a host to AFU MMIO request. Return true on EOP.
//
static bool pcie_tlp_h2a_mem(
    long long cycle,
    int ch,
    t_ase_axis_pcie_tdata *tdata,
    t_ase_axis_pcie_rx_tuser *tuser
)
{
    uint32_t req_dw = 0;
    uint32_t start_dw;
    t_tlp_hdr_upk hdr;
    const mmio_t *mmio_pkt = NULL;

    tdata->valid = 0;
    tdata->sop = 0;
    tdata->eop = 0;

    if (mmio_req_dw_rem)
    {
        // In the middle of an MMIO write
        tdata->valid = 1;
        mmio_pkt = &mmio_req_head->mmio_pkt;

        // Fill the channel or send whatever remains of the packet
        req_dw = mmio_req_dw_rem;
        start_dw = mmio_pkt->width / 32 - mmio_req_dw_rem;
    }
    else if (mmio_req_head)
    {
        // Rate limit MMIO requests, both to be more realistic and as a
        // simple arbitration with DMA read completions. Some CSR code,
        // such as NLB, doesn't deal well with dense MMIO traffic.
        if ((cycle - last_mmio_req_cycle) < 63) return true;

        // Refuse to start a new packet randomly in order to make the
        // channel use pattern more complicated.
        if ((pcie_tlp_rand() & 0xff) > 0xd0) return true;

        mmio_pkt = &mmio_req_head->mmio_pkt;

        tdata->valid = 1;
        tdata->sop = 1;

        tlp_hdr_reset(&hdr);
        hdr.dw0.fmttype = (mmio_pkt->write_en == MMIO_WRITE_REQ) ?
                              PCIE_FMTTYPE_MEM_WRITE32 : PCIE_FMTTYPE_MEM_READ32;
        hdr.dw0.length = mmio_pkt->width / 32;
        hdr.u.mem.tag = mmio_pkt->slot_idx;
        hdr.u.mem.last_be = (mmio_pkt->width <= 32) ? 0 : 0xf;
        hdr.u.mem.first_be = 0xf;
        hdr.u.mem.addr = mmio_pkt->addr;

        tlp_hdr_pack(tdata->hdr, &hdr);
        mmio_req_dw_rem = hdr.dw0.length;

        // Fill the channel or send whatever remains of the packet
        req_dw = hdr.dw0.length;
        start_dw = 0;
    }

    if (tdata->valid)
    {
        last_mmio_req_cycle = cycle;

        if (mmio_pkt->write_en != MMIO_WRITE_REQ)
        {
            // Read has no data
            tdata->eop = 1;
            mmio_req_dw_rem = 0;
            memset(tdata->payload, 0, param_cfg.channel_payload_bytes);
        }
        else
        {
            if (req_dw <= (param_cfg.channel_payload_bytes / 4))
            {
                // Data fits in this flit
                tdata->eop = 1;

                // The app side expects a response for writes in order to
                // track credits.
                mmio_response((mmio_t*)mmio_pkt);
            }
            else
            {
                // More flits required
                tdata->eop = 0;
                req_dw = param_cfg.channel_payload_bytes / 4;
            }

            // Copy the next data group to the channel
            const uint32_t *req_data = (const uint32_t *)mmio_pkt->qword;
            for (int i = 0; i < req_dw; i += 1)
            {
                svPutPartselBit(tdata->payload, req_data[start_dw + i], i*32, 32);
            }

            mmio_req_dw_rem -= req_dw;
        }

        fprintf_tlp_host_to_afu(logfile, cycle, ch, &hdr, tdata, tuser);
    }

    // Pop request
    if (tdata->eop)
    {
        t_mmio_list *m = mmio_req_head;
        mmio_req_head = mmio_req_head->next;
        if (mmio_req_head == NULL)
        {
            mmio_req_tail = NULL;
        }
        ase_free_buffer((char *)m);
    }

    return !tdata->valid || tdata->eop;
}


//
// Process a host to AFU DMA read response.
//
static bool pcie_tlp_h2a_cpld(
    long long cycle,
    int ch,
    t_ase_axis_pcie_tdata *tdata,
    t_ase_axis_pcie_rx_tuser *tuser
)
{
    uint32_t rsp_dw = 0;
    uint32_t start_dw;
    t_dma_read_state *dma_rsp = dma_read_rsp_head;
    t_tlp_hdr_upk hdr;

    tdata->valid = 0;
    tdata->sop = 0;
    tdata->eop = 0;

    if (dma_read_rsp_dw_rem)
    {
        // In the middle of a completion
        tdata->valid = 1;

        // Fill the channel or return whatever remains of the packet
        rsp_dw = dma_read_rsp_dw_rem;
        start_dw = dma_rsp->req_hdr.dw0.length - dma_read_rsp_dw_rem;
    }
    else if (dma_read_rsp_head)
    {
        // Refuse to start a new packet randomly in order to make the
        // channel use pattern more complicated.
        if ((pcie_tlp_rand() & 0xff) > 0xd0) return true;

        tdata->valid = 1;
        tdata->sop = 1;

        tlp_hdr_reset(&hdr);
        hdr.dw0.fmttype = PCIE_FMTTYPE_CPLD;
        hdr.dw0.length = dma_rsp->req_hdr.dw0.length;
        hdr.u.cpl.byte_count = dma_rsp->req_hdr.dw0.length * 4;
        hdr.u.cpl.tag = dma_rsp->req_hdr.u.mem.tag;
        hdr.u.cpl.lower_addr = dma_rsp->req_hdr.u.mem.addr;

        tlp_hdr_pack(tdata->hdr, &hdr);
        dma_read_rsp_dw_rem = dma_rsp->req_hdr.dw0.length;

        // Fill the channel or return whatever remains of the packet
        rsp_dw = dma_rsp->req_hdr.dw0.length;
        start_dw = 0;
    }

    if (tdata->valid)
    {
        if (rsp_dw <= (param_cfg.channel_payload_bytes / 4))
        {
            tdata->eop = 1;
        }
        else
        {
            tdata->eop = 0;
            rsp_dw = param_cfg.channel_payload_bytes / 4;
        }

        // Copy the next data group to the channel
        const uint32_t *rsp_data = read_rsp_data[dma_rsp->req_hdr.u.mem.tag];
        for (int i = 0; i < rsp_dw; i += 1)
        {
            svPutPartselBit(tdata->payload, rsp_data[start_dw + i], i*32, 32);
        }

        dma_read_rsp_dw_rem -= rsp_dw;

        fprintf_tlp_host_to_afu(logfile, cycle, ch, &hdr, tdata, tuser);
    }

    // Pop request
    if (tdata->eop)
    {
        dma_rsp->busy = false;
        dma_read_rsp_head = dma_rsp->next;
        if (dma_read_rsp_head == NULL)
        {
            dma_read_rsp_tail = NULL;
        }
    }

    return !tdata->valid || tdata->eop;
}


// ========================================================================
//
//  DPI-C methods that communicate with the SystemVerilog simulation
//
// ========================================================================

typedef enum
{
    TLP_STATE_NONE,
    TLP_STATE_CPL,
    TLP_STATE_MEM
}
t_tlp_state_enum;

static t_tlp_state_enum afu_to_host_state;
static t_tlp_state_enum host_to_afu_state;

int pcie_param_init(const t_ase_axis_param_cfg *params)
{
    memcpy((char *)&param_cfg, (const char *)params, sizeof(param_cfg));

    ase_free_buffer((char *)mmio_read_state);
    uint32_t mmio_state_size = sizeof(t_mmio_read_state) *
                               param_cfg.max_outstanding_mmio_rd_reqs;
    mmio_read_state = ase_malloc(mmio_state_size);
    memset(mmio_read_state, 0, mmio_state_size);

    ase_free_buffer((char *)dma_read_state);
    uint32_t dma_state_size = sizeof(t_dma_read_state) *
                              param_cfg.max_outstanding_dma_rd_reqs;
    dma_read_state = ase_malloc(dma_state_size);
    memset(dma_read_state, 0, dma_state_size);

    // Free array of buffers for each read tag
    if (read_rsp_n_entries)
    {
        for (int i = 0; i < read_rsp_n_entries; i += 1)
        {
            ase_free_buffer((char *)read_rsp_data[i]);
        }
        ase_free_buffer((char *)read_rsp_data);
    }

    // Allocate a buffer for each possible read tag
    read_rsp_n_entries = param_cfg.max_outstanding_dma_rd_reqs;
    read_rsp_data = ase_malloc(sizeof(void*) * read_rsp_n_entries);
    for (int i = 0; i < read_rsp_n_entries; i += 1)
    {
        read_rsp_data[i] = ase_malloc(param_cfg.max_payload_bytes);
    }

    return 0;
}
                                                       
int pcie_tlp_reset()
{
    if (!in_reset)
    {
        in_reset = true;
    }

    afu_to_host_state = TLP_STATE_NONE;
    host_to_afu_state = TLP_STATE_NONE;

    return 0;
}
                                                       
//
// Get a host->AFU PCIe TLP message for a single channel. Called once per
// cycle via DPI-C for each PCIe channel.
//
int pcie_tlp_stream_host_to_afu_ch(
    long long cycle,
    int ch,
    int tready,
    t_ase_axis_pcie_tdata *tdata,
    t_ase_axis_pcie_rx_tuser *tuser
)
{
    cur_cycle = cycle;
    in_reset = false;

    tdata->valid = 0;

    // Receive pending memory responses from the remote memory model
    if (ch == 0)
    {
        pcie_complete_dma_writes();
        pcie_receive_dma_reads();
    }

    if (!tready) return 0;

    switch (host_to_afu_state)
    {
      case TLP_STATE_NONE:
        if (mmio_req_head && ! pcie_tlp_h2a_mem(cycle, ch, tdata, tuser))
        {
            host_to_afu_state = TLP_STATE_MEM;
        }
        else if (! tdata->valid &&
                 dma_read_rsp_head && ! pcie_tlp_h2a_cpld(cycle, ch, tdata, tuser))
        {
            host_to_afu_state = TLP_STATE_CPL;
        }
        break;

      case TLP_STATE_CPL:
        if (pcie_tlp_h2a_cpld(cycle, ch, tdata, tuser))
        {
            host_to_afu_state = TLP_STATE_NONE;
        }
        break;

      case TLP_STATE_MEM:
        if (pcie_tlp_h2a_mem(cycle, ch, tdata, tuser))
        {
            host_to_afu_state = TLP_STATE_NONE;
        }
        break;

      default:
        ASE_ERR("Unexpected host to AFU TLP state\n");
        start_simkill_countdown();
    }

    return 0;
}

//
// Receive an AFU->host PCIe TLP message for a single channel. Called only
// when a channel has valid data.
//
int pcie_tlp_stream_afu_to_host_ch(
    long long cycle,
    int ch,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
)
{
    t_tlp_hdr_upk hdr;
    tlp_hdr_unpack(&hdr, tdata->hdr);

    fprintf_tlp_afu_to_host(logfile, cycle, ch, &hdr, tdata, tuser);

    switch (afu_to_host_state)
    {
      case TLP_STATE_NONE:
        if (!tdata->sop)
        {
            ASE_ERR("AFU Tx TLP - Non-SOP packet when SOP expected:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
            return 0;
        }
        
        if (tlp_func_is_completion(hdr.dw0.fmttype))
        {
            if (!tlp_func_has_data(hdr.dw0.fmttype))
            {
                ASE_ERR("AFU Tx TLP - Unexpected PCIe completion without data:\n");
                pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
                return 0;
            }

            pcie_tlp_a2h_cpld(cycle, ch, &hdr, tdata, tuser);

            if (!tdata->eop)
            {
                afu_to_host_state = TLP_STATE_CPL;
            }
        }
        else if (tlp_func_is_mem_req(hdr.dw0.fmttype))
        {
            if (tlp_func_is_mwr_req(hdr.dw0.fmttype))
            {
                // DMA write
                pcie_tlp_a2h_mwr(cycle, ch, &hdr, tdata, tuser);
            }
            else
            {
                // DMA read
                pcie_tlp_a2h_mrd(cycle, ch, &hdr, tdata, tuser);
            }

            if (!tdata->eop)
            {
                afu_to_host_state = TLP_STATE_MEM;
            }
        }
        break;

      case TLP_STATE_CPL:
        if (tdata->sop)
        {
            ASE_ERR("AFU Tx TLP - SOP packet in the middle of a multi-beat completion:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
            return 0;
        }

        pcie_tlp_a2h_cpld(cycle, ch, &hdr, tdata, tuser);
        break;

      case TLP_STATE_MEM:
        if (tdata->sop)
        {
            ASE_ERR("AFU Tx TLP - SOP packet in the middle of a multi-beat memory request:\n");
            pcie_tlp_a2h_error_and_kill(cycle, ch, &hdr, tdata, tuser);
            return 0;
        }

        pcie_tlp_a2h_mwr(cycle, ch, &hdr, tdata, tuser);
        break;

      default:
        ASE_ERR("Unexpected AFU to host TLP state\n");
        start_simkill_countdown();
    }

    if (tdata->eop)
    {
        afu_to_host_state = TLP_STATE_NONE;
    }

    return 0;
}


//
// Get the next cycle's tready state for the AFU to host stream.
//
int pcie_tlp_stream_afu_to_host_tready(
    long long cycle
)
{
    cur_cycle = cycle;

    // Random back-pressure
    return ((pcie_tlp_rand() & 0xff) > 0x10);
}


//
// Get a host->AFU PCIe interrupt response. Called once per cycle via DPI-C.
//
int pcie_host_to_afu_irq_rsp(
    long long cycle,
    int tready,
    t_ase_axis_pcie_irq_rsp *irq_rsp
)
{
    irq_rsp->tvalid = 0;

    return 0;
}


//
// Open a log file. The SystemVerilog PCIe emulator and the C code share the
// same log file.
//
int pcie_tlp_open_logfile(
    const char *logname
)
{
    logfile = fopen(logname, "w");
    if (logfile == NULL)
    {
        fprintf(stderr, "Failed to open log file: %s\n", logname);
        logfile = stdout;
    }
    return logfile ? 0 : 1;
}

//
// Write a message to the shared log file.
//
int pcie_tlp_write_logfile(
    const char *msg
)
{
    fprintf(logfile, "%s", msg);
    fflush(logfile);
    return 0;
}
