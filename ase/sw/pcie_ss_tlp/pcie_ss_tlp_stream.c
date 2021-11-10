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

#include <assert.h>

#include "ase_common.h"
#include "ase_host_memory.h"
#include "pcie_ss_tlp_stream.h"

static FILE *logfile;

t_ase_pcie_ss_param_cfg pcie_ss_param_cfg;
t_ase_pcie_ss_cfg pcie_ss_cfg;

static bool in_reset;
static uint64_t cur_cycle;


// ========================================================================
//
//  Utilities
//
// ========================================================================

static unsigned long next_rand = 1;
static bool did_rand_init = false;
static bool unlimited_bw_mode = false;

// Local repeatable random number generator
static int32_t pcie_tlp_rand(void)
{
    if (!did_rand_init)
    {
        did_rand_init = true;
        unlimited_bw_mode = (getenv("ASE_UNLIMITED_BW") != NULL);
    }

    if (unlimited_bw_mode) return 0;

    next_rand = next_rand * 1103515245 + 12345;
    return ((uint32_t)(next_rand/65536) % 32768);
}

// Offset to add to lower_addr due to masked bytes at the start of a read
// completion.
//
// Based on PCIe standard, table 2-39 in 2.3.1.1 Data Return for Read Requests.
static uint8_t pcie_cpl_lower_addr_byte_offset(uint8_t first_be)
{
    uint8_t offset = 0;

    // Zero-length read (fence)?
    if (first_be == 0) return 0;

    // Count zeros in the low bits of first_be mask
    while ((first_be & 1) == 0)
    {
        offset += 1;
        first_be >>= 1;
    }

    return offset;
}

// Byte count for read completion given length and byte masks.
//
// Based on PCIe standard, table 2-38 in 2.3.1.1 Data Return for Read Requests.
static uint16_t pcie_cpl_byte_count(
    uint16_t len_dw,
    uint8_t first_be,
    uint8_t last_be
)
{
    if (first_be == 0)
    {
        // Sanity check. These are supposed to be checked already when
        // reads arrive.
        if ((last_be != 0) || (len_dw != 1))
        {
            fprintf(stderr, "Unexpected last_be and length\n");
            start_simkill_countdown();
            exit(1);
        }

        return 1;
    }

    if (last_be == 0)
    {
        if (len_dw != 1)
        {
            fprintf(stderr, "Unexpected last_be and length\n");
            start_simkill_countdown();
            exit(1);
        }

        last_be = first_be;
    }

    //
    // Byte length is the length (in DWORDS) minus the number of masked
    // out bytes at the beginning and end. At this point we know that
    // both first_be and last_be have at least one bit set.
    //
    uint16_t byte_count = len_dw * 4;

    while ((first_be & 1) == 0)
    {
        byte_count -= 1;
        first_be >>= 1;
    }

    while ((last_be & 0x8) == 0)
    {
        byte_count -= 1;
        last_be <<= 1;
    }

    return byte_count;
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
    uint64_t start_cycle;
    t_pcie_ss_hdr_upk req_hdr;
    bool busy;
} t_dma_read_state;

// Vector of DMA read states, indexed by what must be unique PCIe
// transaction IDs. The number of IDs is set by pcie_param_init(), which
// allocates the vector.
static t_dma_read_state *dma_read_state;

// Read completion may be broken into multiple packets, depending on
// the request completion boundary. A linked list of responses is generated,
// where multiple entries in the list may point to the same t_dma_read_state.
// PCIe requires that they be on the list in increasing address order.
typedef struct dma_read_cpl
{
    struct dma_read_cpl *next;
    struct dma_read_cpl *prev;
    t_dma_read_state *state;
    // Length of this individual packet
    uint16_t len_dw;
    // Offset to the DW of the data for this packet. Non-zero only when
    // the completion is broken into multiple packets.
    uint16_t start_dw;
    // Standard PCIe byte count of all remaining bytes to complete the
    // original request. Byte count may be larger than the length of this
    // packet.
    uint16_t byte_count;
    // Is this the first response packet for the request?
    bool is_first;
    // Is this the last response packet for the request?
    bool is_last;
} t_dma_read_cpl;
static t_dma_read_cpl *dma_read_cpl_head;
static t_dma_read_cpl *dma_read_cpl_tail;

// Number of DWORDs remaining in current host to AFU read completion
static uint32_t dma_read_cpl_dw_rem;

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
    int tlast,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    BEGIN_RED_FONTCOLOR;
    fprintf_pcie_ss_afu_to_host(stdout, cycle, tlast, hdr, tdata, tuser, tkeep);
    END_RED_FONTCOLOR;
    start_simkill_countdown();
}


//
// Process a completion with data flit. Proper SOP placement has already
// been checked.
//
static void pcie_tlp_a2h_cpld(
    long long cycle,
    int tlast,
    const t_pcie_ss_hdr_upk *new_hdr,  // Non-NULL only on SOP
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    static t_pcie_ss_hdr_upk hdr;
    static uint32_t next_dw_idx;
    static uint32_t *payload = NULL;

    if (payload == NULL)
    {
        // Allocate a payload buffer. Initialization should have happened before
        // the first flit arrived, so the configuration state is known.
        assert(pcie_ss_param_cfg.max_rd_req_bytes);
        payload = ase_malloc(pcie_ss_param_cfg.max_rd_req_bytes);
    }

    // First payload DWORD in tdata (after a possible header)
    uint32_t tdata_payload_dw_idx = 0;
    // Data available in tdata
    uint32_t tdata_payload_num_dw = pcie_ss_param_cfg.tdata_width_bits / 32;

    if (new_hdr)
    {
        memcpy(&hdr, new_hdr, sizeof(hdr));
        next_dw_idx = 0;

        tdata_payload_dw_idx = pcie_ss_cfg.tlp_hdr_dwords;
        tdata_payload_num_dw -= pcie_ss_cfg.tlp_hdr_dwords;

        if (hdr.tag > pcie_ss_param_cfg.max_outstanding_mmio_rd_reqs)
        {
            ASE_ERR("AFU Tx TLP - Illegal MMIO read response tag:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if (hdr.len_bytes != hdr.u.cpl.byte_count)
        {
            ASE_ERR("AFU Tx TLP - Split MMIO completion not supported:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if (hdr.len_bytes > pcie_ss_param_cfg.max_rd_req_bytes)
        {
            ASE_ERR("AFU Tx TLP - MMIO completion larger than max payload bytes (%d):\n",
                    pcie_ss_param_cfg.max_rd_req_bytes);
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if (hdr.u.cpl.byte_count > 64)
        {
            ASE_ERR("AFU Tx TLP - MMIO completion larger than 64 bytes not supported:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
    }

    // How many DWORDs (uint32_t) are still expected?
    uint32_t payload_dws = (hdr.len_bytes / 4) - next_dw_idx;
    if (payload_dws > tdata_payload_num_dw)
    {
        // This is not the last flit in the packet.
        if (tlast)
        {
            ASE_ERR("AFU Tx TLP - premature end of MMIO completion:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }

        payload_dws = tdata_payload_num_dw;
    }
    else
    {
        // This should be the last flit in the packet.
        if (!tlast)
        {
            ASE_ERR("AFU Tx TLP - expected EOP in MMIO completion:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
    }

    // Copy payload data
    for (int i = 0; i < payload_dws; i += 1)
    {
        svGetPartselBit(&payload[next_dw_idx + i], tdata, (i + tdata_payload_dw_idx) * 32, 32);
    }
    next_dw_idx += payload_dws;

    // Packet complete?
    if (tlast)
    {
        if (!mmio_read_state[hdr.tag].busy)
        {
            ASE_ERR("AFU Tx TLP - MMIO read response tag is not active:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }

        static mmio_t mmio_pkt;
        mmio_pkt.tid = mmio_read_state[hdr.tag].tid;
        mmio_pkt.write_en = MMIO_READ_REQ;
        mmio_pkt.width = hdr.len_bytes * 8;
        mmio_pkt.addr = hdr.u.cpl.low_addr;
        mmio_pkt.resp_en = 1;
        mmio_pkt.slot_idx = hdr.tag;
        memcpy(mmio_pkt.qword, payload, hdr.len_bytes);
        mmio_read_state[hdr.tag].busy = false;
        
        mmio_response(&mmio_pkt);
    }
}


//
// Process a DMA write request. Proper SOP placement has already
// been checked.
//
static void pcie_tlp_a2h_mwr(
    long long cycle,
    int tlast,
    const t_pcie_ss_hdr_upk *new_hdr,  // Non-NULL only on SOP
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    static t_pcie_ss_hdr_upk hdr;
    static uint32_t next_dw_idx;
    static uint32_t *payload = NULL;

    if (payload == NULL)
    {
        // Allocate a payload buffer. Initialization should have happened before
        // the first flit arrived, so the configuration state is known.
        assert(pcie_ss_param_cfg.max_wr_payload_bytes);
        payload = ase_malloc(pcie_ss_param_cfg.max_wr_payload_bytes);
    }

    // First payload DWORD in tdata (after a possible header)
    uint32_t tdata_payload_dw_idx = 0;
    // Data available in tdata
    uint32_t tdata_payload_num_dw = pcie_ss_param_cfg.tdata_width_bits / 32;

    if (new_hdr)
    {
        memcpy(&hdr, new_hdr, sizeof(hdr));
        next_dw_idx = 0;

        tdata_payload_dw_idx = pcie_ss_cfg.tlp_hdr_dwords;
        tdata_payload_num_dw -= pcie_ss_cfg.tlp_hdr_dwords;

        if (hdr.len_bytes > pcie_ss_param_cfg.max_wr_payload_bytes)
        {
            ASE_ERR("AFU Tx TLP - DMA write larger than max payload bytes (%d):\n",
                    pcie_ss_param_cfg.max_wr_payload_bytes);
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if (hdr.len_bytes == 0)
        {
            ASE_ERR("AFU Tx TLP - DMA write length is 0:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if (hdr.u.req.first_dw_be == 0)
        {
            ASE_ERR("AFU Tx TLP - DMA write first_be is 0:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if ((hdr.len_bytes <= 4) && (hdr.u.req.last_dw_be != 0) && !hdr.dm_mode)
        {
            ASE_ERR("AFU Tx TLP - DMA write last_be must be 0 on single DWORD writes:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if ((hdr.len_bytes > 4) && (hdr.u.req.last_dw_be == 0))
        {
            ASE_ERR("AFU Tx TLP - DMA write last_be is 0 on a multiple DWORD write:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if ((hdr.u.req.addr <= 0xffffffff) && tlp_func_is_addr64(hdr.fmt_type))
        {
            ASE_ERR("AFU Tx TLP - PCIe does not allow 64 bit writes when address fits in MWr32:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
    }

    // How many DWORDs (uint32_t) are still expected?
    uint32_t payload_dws = (hdr.len_bytes / 4) - next_dw_idx;
    if (payload_dws > tdata_payload_num_dw)
    {
        // This is not the last flit in the packet.
        if (tlast)
        {
            ASE_ERR("AFU Tx TLP - premature end of DMA write:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }

        payload_dws = tdata_payload_num_dw;
    }
    else
    {
        // This should be the last flit in the packet.
        if (!tlast)
        {
            ASE_ERR("AFU Tx TLP - expected EOP in DMA write:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
    }

    // Copy payload data
    for (int i = 0; i < payload_dws; i += 1)
    {
        svGetPartselBit(&payload[next_dw_idx + i], tdata, (i + tdata_payload_dw_idx) * 32, 32);
    }
    next_dw_idx += payload_dws;

    // Packet complete?
    if (tlast)
    {
        // Write to memory
        ase_host_memory_write_req wr_req;
        wr_req.req = HOST_MEM_REQ_WRITE;
        wr_req.data_bytes = hdr.len_bytes;
        wr_req.addr = hdr.u.req.addr;
        if ((hdr.u.req.first_dw_be != 0xf) || ((hdr.len_bytes > 4) && (hdr.u.req.last_dw_be != 0xf)))
        {
            wr_req.byte_en = 1;
            wr_req.first_be = hdr.u.req.first_dw_be;
            wr_req.last_be = hdr.u.req.last_dw_be;
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
    int tlast,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    if (!tlast)
    {
        ASE_ERR("AFU Tx TLP - expected EOP with DMA read request:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if (hdr->len_bytes > pcie_ss_param_cfg.max_rd_req_bytes)
    {
        ASE_ERR("AFU Tx TLP - DMA read larger than max payload bytes (%d):\n",
                pcie_ss_param_cfg.max_rd_req_bytes);
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if (hdr->len_bytes == 0)
    {
        ASE_ERR("AFU Tx TLP - DMA read length is 0:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if ((hdr->u.req.first_dw_be == 0) &&
        ((hdr->u.req.last_dw_be != 0) || (hdr->len_bytes > 4)))
    {
        ASE_ERR("AFU Tx TLP - DMA read first_be is 0 and not a zero-length read (fence):\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if ((hdr->len_bytes <= 4) && (hdr->u.req.last_dw_be != 0) && !hdr->dm_mode)
    {
        ASE_ERR("AFU Tx TLP - DMA read last_be must be 0 on single DWORD reads:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if ((hdr->len_bytes > 4) && (hdr->u.req.last_dw_be == 0))
    {
        ASE_ERR("AFU Tx TLP - DMA read last_be is 0 on a multiple DWORD read:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if ((hdr->u.req.addr <= 0xffffffff) && tlp_func_is_addr64(hdr->fmt_type))
    {
        ASE_ERR("AFU Tx TLP - PCIe does not allow 64 bit reads when address fits in MRd32:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if (func_is_atomic_req(hdr->fmt_type))
    {
        if (hdr->dm_mode)
        {
            ASE_ERR("AFU Tx TLP - Atomic functions must be PU encoded:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
        }

        if (func_is_atomic_cas_req(hdr->fmt_type))
        {
            if ((hdr->len_bytes != 8) && (hdr->len_bytes != 16))
            {
                ASE_ERR("AFU Tx TLP - Atomic CAS must specify either 8 or 16 bytes:\n");
                pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
            }
        }
        else if ((hdr->len_bytes != 4) && (hdr->len_bytes != 8))
        {
            ASE_ERR("AFU Tx TLP - Atomic FAdd and SWAP must specify either 4 or 8 bytes:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
        }

        if ((hdr->u.req.first_dw_be != 0xf) || ((hdr->len_bytes > 4) && (hdr->u.req.last_dw_be != 0xf)))
        {
            ASE_ERR("AFU Tx TLP - Atomic functions may not use FBE/LBE masks:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
        }
    }

    if (hdr->tag >= pcie_ss_param_cfg.max_outstanding_dma_rd_reqs)
    {
        ASE_ERR("AFU Tx TLP - Illegal DMA read request tag:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    uint32_t tag = hdr->tag;
    if (dma_read_state[tag].busy)
    {
        ASE_ERR("AFU Tx TLP - DMA read request tag already in use:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    // Record read request
    dma_read_state[tag].busy = true;
    dma_read_state[tag].start_cycle = cycle;
    memcpy(&dma_read_state[tag].req_hdr, hdr, sizeof(t_pcie_ss_hdr_upk));

    static ase_host_memory_read_req rd_req;
    rd_req.req = HOST_MEM_REQ_READ;
    rd_req.addr = hdr->u.req.addr;
    if ((hdr->len_bytes <= 4) && ! hdr->u.req.last_dw_be && ! hdr->u.req.first_dw_be)
    {
        // Single word read with all bytes disabled -- a fence
        rd_req.data_bytes = 0;
    }
    else
    {
        rd_req.data_bytes = hdr->len_bytes;
    }
    rd_req.tag = tag;

    if (func_is_atomic_req(hdr->fmt_type))
    {
        // Set the atomic function and pass the source operands
        rd_req.req = HOST_MEM_REQ_ATOMIC;

        if (func_is_atomic_cas_req(hdr->fmt_type))
        {
            // Completion payload of atomic CAS is half the request size
            rd_req.data_bytes >>= 1;
            dma_read_state[tag].req_hdr.len_bytes >>= 1;
        }

        // Extract possible operands into 32 bit chunks
        uint32_t atomic_opers[4];
        for (int i = 0; i < 4; i += 1)
        {
            svGetPartselBit(&atomic_opers[i], tdata, (i + pcie_ss_cfg.tlp_hdr_dwords) * 32, 32);
        }

        switch (hdr->fmt_type)
        {
          case PCIE_FMTTYPE_FETCH_ADD32:
            rd_req.atomic_func = HOST_MEM_ATOMIC_OP_FETCH_ADD;
            rd_req.atomic_wr_data[0] = atomic_opers[0];
            rd_req.atomic_wr_data[1] = 0;
            break;
          case PCIE_FMTTYPE_FETCH_ADD64:
            rd_req.atomic_func = HOST_MEM_ATOMIC_OP_FETCH_ADD;
            rd_req.atomic_wr_data[0] = ((uint64_t)atomic_opers[1] << 32) | atomic_opers[0];
            rd_req.atomic_wr_data[1] = 0;
            break;
          case PCIE_FMTTYPE_SWAP32:
            rd_req.atomic_func = HOST_MEM_ATOMIC_OP_SWAP;
            rd_req.atomic_wr_data[0] = atomic_opers[0];
            rd_req.atomic_wr_data[1] = 0;
            break;
          case PCIE_FMTTYPE_SWAP64:
            rd_req.atomic_func = HOST_MEM_ATOMIC_OP_SWAP;
            rd_req.atomic_wr_data[0] = ((uint64_t)atomic_opers[1] << 32) | atomic_opers[0];
            rd_req.atomic_wr_data[1] = 0;
            break;
          case PCIE_FMTTYPE_CAS32:
          case PCIE_FMTTYPE_CAS64:
            rd_req.atomic_func = HOST_MEM_ATOMIC_OP_CAS;
            if (rd_req.data_bytes == 4)
            {
                rd_req.atomic_wr_data[0] = atomic_opers[0];
                rd_req.atomic_wr_data[1] = atomic_opers[1];
            }
            else
            {
                rd_req.atomic_wr_data[0] = ((uint64_t)atomic_opers[1] << 32) | atomic_opers[0];
                rd_req.atomic_wr_data[1] = ((uint64_t)atomic_opers[3] << 32) | atomic_opers[2];
            }
            break;
          default:
            ASE_ERR("Unexpected atomic function:\n");
            pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
        }
    }

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


//
// Pick a random read completion length in order to simulate PCIe breaking
// apart completions in read-completion-boundary-sized chunks or larger.
//
static inline uint32_t random_cpl_length(uint32_t len_dw_rem)
{
    uint32_t length;

    if (len_dw_rem > (pcie_ss_param_cfg.request_completion_boundary / 4))
    {
        uint32_t max_chunks;
        uint32_t rand_num;
        uint32_t rand_chunks;
        uint32_t rand_length;

        // Pick a random length, between the RCB and the total
        // payload size.
        max_chunks = pcie_ss_param_cfg.max_rd_req_bytes /
                     pcie_ss_param_cfg.request_completion_boundary;

        // rand_num == 0 is handled as a special case, used when forcing max.
        // bandwidth.
        rand_num = pcie_tlp_rand();
        rand_chunks = (rand_num == 0) ? max_chunks : 1 + (rand_num % max_chunks);

        // Random length (in DWORDs, like len_dw_rem)
        rand_length = rand_chunks * pcie_ss_param_cfg.request_completion_boundary / 4;

        // Limit length to the random number of chunks
        length = (len_dw_rem <= rand_length) ? len_dw_rem : rand_length;
    }
    else
    {
        length = len_dw_rem;
    }

    return length;
}


//
// Push a read completion onto the list of pending host to AFU completions.
//
static void push_new_read_cpl(t_dma_read_cpl *read_cpl)
{
    // Add some randomization of read completion ordering. PCIe allows reordering
    // of read responses, except that when a request is broken into multiple
    // responses those responses must be ordered relative to each other.

    // Pick a random number of current responses to put after this new one
    uint32_t r = pcie_tlp_rand() & 0xff;
    int n_later_rsp;
    // r == 0 is a special case, used when rate limiting is off
    if ((r >= 0x80) || (r == 0))
        n_later_rsp = 0;
    else if (r >= 0x20)
        n_later_rsp = 5;
    else if (r >= 0x10)
        n_later_rsp = 2;
    else
        n_later_rsp = 1;

    // Walk back n_later_rsp responses
    t_dma_read_cpl *prev_cpl = dma_read_cpl_tail;
    while (n_later_rsp--)
    {
        if (NULL != prev_cpl)
        {
            // Responses for the same request? Can't reorder then.
            if (prev_cpl->state == read_cpl->state) break;

            // Never switch with head. It might already be in the middle of
            // a response to the AFU.
            if (prev_cpl == dma_read_cpl_head) break;

            prev_cpl = prev_cpl->prev;
        }
    }

    // Put the new read completion after prev_cpl.
    read_cpl->prev = prev_cpl;
    if (prev_cpl != NULL)
    {
        read_cpl->next = prev_cpl->next;
        prev_cpl->next = read_cpl;
    }
    else
    {
        read_cpl->next = NULL;
        dma_read_cpl_head = read_cpl;
    }

    if (read_cpl->next != NULL)
    {
        read_cpl->next->prev = read_cpl;
    }
    else
    {
        dma_read_cpl_tail = read_cpl;
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
            t_dma_read_state *rd_state = &dma_read_state[rd_rsp.tag];
            const t_pcie_ss_hdr_upk *req_hdr = &rd_state->req_hdr;

            //
            // If the read response is large then it might be broken apart into
            // multiple response packets. Simulate that, randomizing the size
            // decisions.
            //

            // Total DWORD length of the response
            uint32_t len_dw_rem = req_hdr->len_bytes / 4;
            // Total bytes of the response, accounting for masks
            uint32_t byte_count_rem =
                pcie_cpl_byte_count(len_dw_rem,
                                    req_hdr->u.req.first_dw_be,
                                    req_hdr->u.req.last_dw_be);
            // Offset of the read data for the current packet
            uint32_t start_dw = 0;

            // Loop until the entire payload has completion packets
            do
            {
                t_dma_read_cpl *read_cpl = ase_malloc(sizeof(t_dma_read_cpl));
                read_cpl->state = rd_state;

                // Pick a random length, between the request completion
                // boundary and the total payload size.
                uint32_t this_len_dw = random_cpl_length(len_dw_rem);

                bool is_first = (start_dw == 0);
                bool is_last = (this_len_dw == len_dw_rem);

                read_cpl->len_dw = this_len_dw;
                read_cpl->start_dw = start_dw;
                // PCIe expects the total remaining byte count for this and all
                // future packets for the original request as "byte_count".
                read_cpl->byte_count = byte_count_rem;
                read_cpl->is_first = is_first;
                read_cpl->is_last = is_last;

                // Push this completion on the list of pending messages
                push_new_read_cpl(read_cpl);

                // Subtract the length of the current completion from the total
                byte_count_rem = byte_count_rem -
                    pcie_cpl_byte_count(this_len_dw,
                                        is_first ? req_hdr->u.req.first_dw_be : 0xf,
                                        is_last ? req_hdr->u.req.last_dw_be : 0xf);
                len_dw_rem -= this_len_dw;
                start_dw += this_len_dw;

                // Check the algorithm -- is_last implies no remaining data.
                assert((len_dw_rem == 0) == is_last);
            }
            while (len_dw_rem > 0);

            // The algorithm above is broken if this assertion fails. All
            // bytes should have been handled.
            assert(byte_count_rem == 0);
        }
        else if (status != ASE_MSG_ABSENT)
        {
            break;
        }
    }
}


//
// Process an interrupt request.
//
static void pcie_tlp_a2h_interrupt(
    long long cycle,
    int tlast,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    uint32_t irq_id = hdr->u.intr.vector_num;

    if (!tlast)
    {
        ASE_ERR("AFU Tx TLP - expected EOP with interrupt request:\n");
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    if (irq_id >= pcie_ss_param_cfg.num_afu_interrupts)
    {
        ASE_ERR("AFU Tx TLP - IRQ ID too high (max %d):\n", pcie_ss_param_cfg.num_afu_interrupts);
        pcie_tlp_a2h_error_and_kill(cycle, tlast, hdr, tdata, tuser, tkeep);
    }

    ase_interrupt_generator(irq_id);
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
void pcie_ss_mmio_new_req(const mmio_t *pkt)
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

        if (m->mmio_pkt.slot_idx >= pcie_ss_param_cfg.max_outstanding_mmio_rd_reqs)
        {
            ASE_ERR("MMIO read request slot IDX (%d) exceeds max MMIO read IDX (%d)\n",
                    m->mmio_pkt.slot_idx, pcie_ss_param_cfg.max_outstanding_mmio_rd_reqs);
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
    int *tvalid,
    int *tlast,
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
)
{
    uint32_t req_dw = 0;
    uint32_t start_dw = 0;
    uint32_t tdata_start_dw = 0;
    t_pcie_ss_hdr_upk hdr;
    const mmio_t *mmio_pkt = NULL;
    bool sop = 0;

    *tvalid = 0;
    *tlast = 0;

    if (mmio_req_dw_rem)
    {
        // In the middle of an MMIO write
        *tvalid = 1;
        mmio_pkt = &mmio_req_head->mmio_pkt;

        // Fill the channel or send whatever remains of the packet
        req_dw = mmio_req_dw_rem;
        start_dw = mmio_pkt->width / 32 - mmio_req_dw_rem;
        tdata_start_dw = 0;

        pcie_ss_tlp_payload_reset(tdata, tuser, tkeep);
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

        *tvalid = 1;

        pcie_ss_tlp_hdr_reset(&hdr);
        hdr.fmt_type = (mmio_pkt->write_en == MMIO_WRITE_REQ) ?
                         PCIE_FMTTYPE_MEM_WRITE32 : PCIE_FMTTYPE_MEM_READ32;
        hdr.len_bytes = mmio_pkt->width / 8;
        hdr.tag = mmio_pkt->slot_idx;
        hdr.u.req.last_dw_be = (mmio_pkt->width <= 32) ? 0 : 0xf;
        hdr.u.req.first_dw_be = 0xf;
        hdr.u.req.addr = mmio_pkt->addr;

        // Force VF0 for now
        hdr.vf_active = 1;

        pcie_ss_tlp_hdr_pack(tdata, tuser, tkeep, &hdr);
        sop = 1;
        mmio_req_dw_rem = hdr.len_bytes / 4;

        // Fill the channel or send whatever remains of the packet
        req_dw = mmio_req_dw_rem;
        start_dw = 0;
        tdata_start_dw = pcie_ss_cfg.tlp_hdr_dwords;
    }

    if (*tvalid)
    {
        last_mmio_req_cycle = cycle;

        if (mmio_pkt->write_en != MMIO_WRITE_REQ)
        {
            // Read has no data
            *tlast = 1;
            mmio_req_dw_rem = 0;
        }
        else
        {
            if (req_dw <= (pcie_ss_cfg.tlp_tdata_dwords - tdata_start_dw))
            {
                // Data fits in this flit
                *tlast = 1;

                // The app side expects a response for writes in order to
                // track credits.
                mmio_response((mmio_t*)mmio_pkt);
            }
            else
            {
                // More flits required
                req_dw = pcie_ss_cfg.tlp_tdata_dwords - tdata_start_dw;
            }

            // Copy the next data group to the channel
            const uint32_t *req_data = (const uint32_t *)mmio_pkt->qword;
            for (int i = 0; i < req_dw; i += 1)
            {
                svPutPartselBit(tdata, req_data[start_dw + i], (i + tdata_start_dw) * 32, 32);
                svPutPartselBit(tkeep, ~0, (i + tdata_start_dw) * 4, 4);
            }

            mmio_req_dw_rem -= req_dw;
        }

        fprintf_pcie_ss_host_to_afu(logfile, cycle, *tlast,
                                    (sop ? &hdr : NULL),
                                    tdata, tuser, tkeep);
    }

    // Pop request
    if (*tlast)
    {
        t_mmio_list *m = mmio_req_head;
        mmio_req_head = mmio_req_head->next;
        if (mmio_req_head == NULL)
        {
            mmio_req_tail = NULL;
        }
        free(m);
    }

    return !*tvalid || *tlast;
}


//
// Process a host to AFU DMA read response.
//
static bool pcie_tlp_h2a_cpld(
    long long cycle,
    int *tvalid,
    int *tlast,
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
)
{
    uint32_t rsp_dw = 0;
    uint32_t start_dw;
    uint32_t tdata_start_dw = 0;
    t_dma_read_cpl *dma_cpl = dma_read_cpl_head;
    const t_pcie_ss_hdr_upk *req_hdr = NULL;
    t_pcie_ss_hdr_upk hdr;
    bool sop = 0;

    *tvalid = 0;
    *tlast = 0;

    if (dma_read_cpl_dw_rem)
    {
        // Refuse to continue randomly in order to make the
        // channel use pattern more complicated.
        if ((pcie_tlp_rand() & 0xff) > 0xd0) return false;

        // In the middle of a completion
        *tvalid = 1;

        // Fill the channel or return whatever remains of the packet
        rsp_dw = dma_read_cpl_dw_rem;
        req_hdr = &dma_cpl->state->req_hdr;
        start_dw = dma_cpl->start_dw + dma_cpl->len_dw - dma_read_cpl_dw_rem;
        tdata_start_dw = 0;

        pcie_ss_tlp_payload_reset(tdata, tuser, tkeep);
    }
    else if (dma_read_cpl_head)
    {
        // Refuse to start a new packet randomly in order to make the
        // channel use pattern more complicated.
        if ((pcie_tlp_rand() & 0xff) > 0xd0) return true;

        // Minimum latency
        if ((cycle - dma_cpl->state->start_cycle < 250) && !unlimited_bw_mode)
        {
            return true;
        }

        *tvalid = 1;

        req_hdr = &dma_cpl->state->req_hdr;
        pcie_ss_tlp_hdr_reset(&hdr);
        hdr.dm_mode = req_hdr->dm_mode;
        hdr.fmt_type = PCIE_FMTTYPE_CPLD;
        hdr.len_bytes = dma_cpl->len_dw * 4;
        hdr.u.cpl.byte_count = dma_cpl->byte_count;
        hdr.u.cpl.fc = dma_cpl->is_last;
        hdr.tag = req_hdr->tag;
        hdr.u.cpl.low_addr = req_hdr->u.req.addr + (dma_cpl->start_dw * 4);
        if (dma_cpl->is_first)
        {
            hdr.u.cpl.low_addr += pcie_cpl_lower_addr_byte_offset(req_hdr->u.req.first_dw_be);
        }

        pcie_ss_tlp_hdr_pack(tdata, tuser, tkeep, &hdr);
        sop = 1;
        dma_read_cpl_dw_rem = dma_cpl->len_dw;

        // Fill the channel or return whatever remains of the packet
        rsp_dw = dma_cpl->len_dw;
        start_dw = dma_cpl->start_dw;
        tdata_start_dw = pcie_ss_cfg.tlp_hdr_dwords;
    }

    if (*tvalid)
    {
        if (rsp_dw <= (pcie_ss_cfg.tlp_tdata_dwords - tdata_start_dw))
        {
            *tlast = 1;
        }
        else
        {
            *tlast = 0;
            rsp_dw = pcie_ss_cfg.tlp_tdata_dwords - tdata_start_dw;
        }

        // Copy the next data group to the channel
        const uint32_t *rsp_data = read_rsp_data[req_hdr->tag];
        for (int i = 0; i < rsp_dw; i += 1)
        {
            svPutPartselBit(tdata, rsp_data[start_dw + i], (i + tdata_start_dw) * 32, 32);
            svPutPartselBit(tkeep, ~0, (i + tdata_start_dw) * 4, 4);
        }

        dma_read_cpl_dw_rem -= rsp_dw;

        fprintf_pcie_ss_host_to_afu(logfile, cycle, *tlast,
                                    (sop ? &hdr : NULL),
                                    tdata, tuser, tkeep);
    }

    // Pop request
    if (*tlast)
    {
        // Last packet for the original request? If so, free the tag. There
        // may be multiple completion packets associated with a single read request.
        if (dma_cpl->is_last)
        {
            dma_cpl->state->busy = false;
        }

        dma_read_cpl_head = dma_cpl->next;
        if (dma_read_cpl_head == NULL)
        {
            dma_read_cpl_tail = NULL;
        }
        else
        {
            dma_read_cpl_head->prev = NULL;
        }

        free(dma_cpl);
    }

    return !*tvalid || *tlast;
}


// ========================================================================
//
//  DPI-C methods that communicate with the SystemVerilog simulation
//
// ========================================================================

typedef enum
{
    TLP_STATE_SOP,
    TLP_STATE_CPL,
    TLP_STATE_MEM
}
t_pcie_ss_state_enum;

static t_pcie_ss_state_enum afu_to_host_state;
static t_pcie_ss_state_enum host_to_afu_state;

int pcie_ss_param_init(const t_ase_pcie_ss_param_cfg *params)
{
    memcpy((char *)&pcie_ss_param_cfg, (const char *)params, sizeof(pcie_ss_param_cfg));

    pcie_ss_cfg.tlp_hdr_dwords = 8;
    pcie_ss_cfg.tlp_tdata_dwords = pcie_ss_param_cfg.tdata_width_bits / 32;

    free(mmio_read_state);
    uint64_t mmio_state_size = sizeof(t_mmio_read_state) *
                               pcie_ss_param_cfg.max_outstanding_mmio_rd_reqs;
    mmio_read_state = ase_malloc(mmio_state_size);
    memset(mmio_read_state, 0, mmio_state_size);

    free(dma_read_state);
    while (dma_read_cpl_head)
    {
        t_dma_read_cpl *read_cpl_next = dma_read_cpl_head->next;
        free(dma_read_cpl_head);
        dma_read_cpl_head = read_cpl_next;
    }
    dma_read_cpl_tail = NULL;
    uint64_t dma_state_size = sizeof(t_dma_read_state) *
                              pcie_ss_param_cfg.max_outstanding_dma_rd_reqs;
    dma_read_state = ase_malloc(dma_state_size);
    memset(dma_read_state, 0, dma_state_size);
    dma_read_cpl_dw_rem = 0;

    // Free array of buffers for each read tag
    if (read_rsp_n_entries)
    {
        for (int i = 0; i < read_rsp_n_entries; i += 1)
        {
            free(read_rsp_data[i]);
        }
        free(read_rsp_data);
    }

    // Allocate a buffer for each possible read tag
    read_rsp_n_entries = pcie_ss_param_cfg.max_outstanding_dma_rd_reqs;
    read_rsp_data = ase_malloc(sizeof(void*) * read_rsp_n_entries);
    for (int i = 0; i < read_rsp_n_entries; i += 1)
    {
        read_rsp_data[i] = ase_malloc(pcie_ss_param_cfg.max_rd_req_bytes);
    }

    return 0;
}
                                                       
int pcie_ss_reset()
{
    if (!in_reset)
    {
        in_reset = true;
    }

    afu_to_host_state = TLP_STATE_SOP;
    host_to_afu_state = TLP_STATE_SOP;

    return 0;
}
                                                       
//
// Get a host->AFU PCIe TLP message for a single channel. Called once per
// cycle via DPI-C for each PCIe channel.
//
int pcie_ss_stream_host_to_afu(
    long long cycle,
    int tready,
    int *tvalid,
    int *tlast,
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
)
{
    cur_cycle = cycle;
    in_reset = false;

    *tvalid = 0;

    // Receive pending memory responses from the remote memory model
    pcie_complete_dma_writes();
    pcie_receive_dma_reads();

    if (!tready) return 0;

    switch (host_to_afu_state)
    {
      case TLP_STATE_SOP:
        if (mmio_req_head && ! pcie_tlp_h2a_mem(cycle, tvalid, tlast, tdata, tuser, tkeep))
        {
            host_to_afu_state = TLP_STATE_MEM;
        }
        else if (!*tvalid &&
                 dma_read_cpl_head && ! pcie_tlp_h2a_cpld(cycle, tvalid, tlast, tdata, tuser, tkeep))
        {
            host_to_afu_state = TLP_STATE_CPL;
        }
        break;

      case TLP_STATE_CPL:
        if (pcie_tlp_h2a_cpld(cycle, tvalid, tlast, tdata, tuser, tkeep))
        {
            host_to_afu_state = TLP_STATE_SOP;
        }
        break;

      case TLP_STATE_MEM:
        if (pcie_tlp_h2a_mem(cycle, tvalid, tlast, tdata, tuser, tkeep))
        {
            host_to_afu_state = TLP_STATE_SOP;
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
int pcie_ss_stream_afu_to_host(
    long long cycle,
    int valid,
    int tlast,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    t_pcie_ss_hdr_upk hdr;

    switch (afu_to_host_state)
    {
      case TLP_STATE_SOP:
        pcie_ss_tlp_hdr_unpack(&hdr, tdata, tuser, tkeep);
        fprintf_pcie_ss_afu_to_host(logfile, cycle, tlast, &hdr, tdata, tuser, tkeep);
        
        if (tlp_func_is_interrupt_req(hdr.fmt_type))
        {
            pcie_tlp_a2h_interrupt(cycle, tlast, &hdr, tdata, tuser, tkeep);
        }
        if (tlp_func_is_completion(hdr.fmt_type))
        {
            if (!tlp_func_has_data(hdr.fmt_type))
            {
                ASE_ERR("AFU Tx TLP - Unexpected PCIe completion without data:\n");
                pcie_tlp_a2h_error_and_kill(cycle, tlast, &hdr, tdata, tuser, tkeep);
                return 0;
            }

            pcie_tlp_a2h_cpld(cycle, tlast, &hdr, tdata, tuser, tkeep);

            if (!tlast)
            {
                afu_to_host_state = TLP_STATE_CPL;
            }
        }
        else if (tlp_func_is_mem_req(hdr.fmt_type))
        {
            if (tlp_func_is_mwr_req(hdr.fmt_type) && !func_is_atomic_req(hdr.fmt_type))
            {
                // DMA write
                pcie_tlp_a2h_mwr(cycle, tlast, &hdr, tdata, tuser, tkeep);
            }
            else
            {
                // DMA read
                pcie_tlp_a2h_mrd(cycle, tlast, &hdr, tdata, tuser, tkeep);
            }

            if (!tlast)
            {
                afu_to_host_state = TLP_STATE_MEM;
            }
        }
        break;

      case TLP_STATE_CPL:
        fprintf_pcie_ss_afu_to_host(logfile, cycle, tlast, NULL, tdata, tuser, tkeep);
        pcie_tlp_a2h_cpld(cycle, tlast, NULL, tdata, tuser, tkeep);
        break;

      case TLP_STATE_MEM:
        fprintf_pcie_ss_afu_to_host(logfile, cycle, tlast, NULL, tdata, tuser, tkeep);
        pcie_tlp_a2h_mwr(cycle, tlast, NULL, tdata, tuser, tkeep);
        break;

      default:
        ASE_ERR("Unexpected AFU to host TLP state\n");
        start_simkill_countdown();
    }

    if (tlast)
    {
        afu_to_host_state = TLP_STATE_SOP;
    }

    return 0;
}


//
// Get the next cycle's tready state for the AFU to host stream.
//
int pcie_ss_stream_afu_to_host_tready(
    long long cycle
)
{
    cur_cycle = cycle;

    // Random back-pressure
    return ((pcie_tlp_rand() & 0xff) < 0xf0);
}


//
// Open a log file. The SystemVerilog PCIe emulator and the C code share the
// same log file.
//
int pcie_ss_open_logfile(
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
int pcie_ss_write_logfile(
    const char *msg
)
{
    fprintf(logfile, "%s", msg);
    fflush(logfile);
    return 0;
}
