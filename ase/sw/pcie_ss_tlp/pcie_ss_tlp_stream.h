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

#ifndef _PCIE_SS_TLP_STREAM_H_
#define _PCIE_SS_TLP_STREAM_H_

#include "ase_common.h"
#include "pcie_tlp_func.h"


// ========================================================================
//
//  PCIe TLP header types
//
// ========================================================================

//
// Map encoded headers to a C struct that is easier to manipulate.
// C doesn't have a standard for bit-level struct packing, so we can't
// rely on a struct to represent an encoded TLP header.
//
// Encoded TLP headers are defined in the FIM's pcie_ss_hdr_pkg.sv, copied
// into the comments below.
//

//
//
// Encoded power user memory request header:
//
// typedef struct packed {
//     // Byte 31 - Byte 28
//     logic   [31:0]      metadata_l;         // [DW7 31: 0] meta-data field
//
//     // Byte 27 - Byte 24
//     logic   [31:0]      metadata_h;         // [DW6 31: 0] meta-data field
//
//     // Byte 23 - Byte 20
//     logic   [6:0]       bar_number;         // [DW5 31:25] Bar Number
//     logic               mm_mode;            // [DW5 24:24] Memory Mapped mode
//     logic   [4:0]       slot_num;           // [DW5 23:19] Slot Number
//     logic   [3:0]       rsvd2;              // [DW5 18:15] Reserved
//     logic               vf_active;          // [DW5 14:14]
//     logic   [10:0]      vf_num;             // [DW5 13: 3]
//     logic   [2:0]       pf_num;             // [DW5  2: 0]
//
//     // Byte 19 - Byte 16
//     logic   [1:0]       rsvd3;              // [DW4 31:30] Reserved
//     logic               pref_present;       // [DW4 29:29] Prefix Present
//     logic   [4:0]       pref_type;          // [DW4 28:24] Prefix Type
//     logic   [23:0]      pref;               // [DW4 23: 0] Prefix
//
//     // Byte 15 - Byte 12
//     logic   [29:0]      host_addr_l;        // [DW3 31: 2] HostAddress[31:2]
//     logic   [1:0]       PH;                 // [DW3  1: 0]
//
//     // Byte 11 - Byte 8
//     logic   [31:0]      host_addr_h;        // [DW2 31: 0] HostAddress[63:31]
//
//     // Byte 7 - Byte 4
//     logic   [15:0]      req_id;             // [DW1 31:16] Requester ID
//     logic   [7:0]       tag_l;              // [DW1 15: 8] Tag[7:0]
//     logic   [3:0]       last_dw_be;         // [DW1  7: 4] Last DW BE
//     logic   [3:0]       first_dw_be;        // [DW1  3: 0] First DW BE
//
//     // Byte 3 - Byte 0
//     ReqHdr_FmtType_e    fmt_type;           // [DW0 31:24] Specify the type (read/write) - 8 bits wide
//     logic               tag_h;              // [DW0 23:23] Tag[9]
//     logic   [2:0]       TC;                 // [DW0 22:20] Traffic Channel
//     logic               tag_m;              // [DW0 19:19] Tag[8]
//     ReqHdr_Attr_t       attr;               // [DW0 18:10] Attribute Bits - 9 bits wide
//     logic   [9:0]       length;             // [DW0  9: 0] Length in DW
//
// } PCIe_PUReqHdr_t;
//

//
// Encoded power user completion header:
//
// typedef struct packed {
//     // Byte 31 - Byte 28
//     logic   [31:0]      metadata_l;         // [DW7 31: 0] metadata[31:0]
//
//     // Byte 27 - Byte 24
//     logic   [31:0]      metadata_h;         // [DW6 31: 0] metadata[63:32]
//
//     // Byte 23 - Byte 20
//     logic   [6:0]       rsvd1;              // [DW5 31:25] Reserved
//     logic               mm_mode;            // [DW5 24:24] Memory Mapped mode
//     logic   [4:0]       slot_num;           // [DW5 23:19] Slot Number
//     logic   [3:0]       rsvd2;              // [DW5 18:15] Reserved
//     logic               vf_active;          // [DW5 14:14] VF Active
//     logic   [10:0]      vf_num;             // [DW5 13: 3] VF Number
//     logic   [2:0]       pf_num;             // [DW5  2: 0] PF Number
//
//     // Byte 19 - Byte 16
//     logic   [1:0]       rsvd3;              // [DW4 31:30] Reserved
//     logic               pref_present;       // [DW4 29:29] Prefix Present
//     logic   [4:0]       pref_type;          // [DW4 28:24] Prefix Type
//     logic   [23:0]      pref;               // [DW4 23: 0] Prefix
//
//     // Byte 15 - Byte 12
//     logic   [31:0]      rsvd4;              // [DW3 31: 0] Reserved
//
//     // Byte 11 - Byte 8
//     logic   [15:0]      req_id;             // [DW2 31:16] Requester ID
//     logic   [7:0]       tag_l;              // [DW2 15: 8] Tag[7:0]
//     logic               rsvd5;              // [DW2  7: 7] Reserved
//     logic   [6:0]       low_addr;           // [DW2  6: 0] LowerAddress
//
//     // Byte 7 - Byte 4
//     logic   [15:0]      comp_id;            // [DW1 31:16] Completer ID
//     logic   [2:0]       cpl_status;         // [DW1 15:13] Completion Status
//     logic               bcm;                // [DW1 12:12] BCM
//     logic   [11:0]      byte_count;         // [DW1 11: 0] Byte Count
//
//     // Byte 3 - Byte 0
//     ReqHdr_FmtType_e    fmt_type;           // [DW0 31:24] Specify the type (read/write) - 8 bits wide
//     logic               tag_h;              // [DW0 23:23] Tag[9]
//     logic   [2:0]       TC;                 // [DW0 22:20] Traffic Channel
//     logic               tag_m;              // [DW0 19:19] Tag[8]
//     ReqHdr_Attr_t       attr;               // [DW0 18:10] Attribute Bits - 9 bits wide
//     logic   [9:0]       length;             // [DW0  9: 0] Length
//
// } PCIe_PUCplHdr_t;
//

// Header fields used only in requests
typedef struct
{
    uint64_t addr;
    uint8_t last_dw_be;
    uint8_t first_dw_be;
}
t_pcie_ss_hdr_req_upk;

// Header fields used only in completions
typedef struct
{
    uint16_t comp_id;
    uint8_t cpl_status;
    uint8_t bcm;
    uint16_t byte_count;
    uint8_t low_addr;
}
t_pcie_ss_hdr_cpl_upk;

typedef struct
{
    uint64_t metadata;

    uint8_t bar_number;
    uint8_t mm_mode;
    uint8_t slot_num;
    uint8_t vf_active;
    uint16_t vf_num;
    uint8_t pf_num;

    uint16_t req_id;
    uint16_t tag;

    uint8_t fmt_type;
    uint16_t length;

    union {
        t_pcie_ss_hdr_req_upk req;
        t_pcie_ss_hdr_cpl_upk cpl;
    } u;
}
t_pcie_ss_hdr_upk;


//
// Encode PF/VF in req_id/comp_id format.
//
static inline uint32_t pcie_ss_enc_vf_id(
    uint32_t vf_num,
    bool vf_active,
    uint32_t pf_num
)
{
    return (vf_num << 4) | (vf_active << 3) | pf_num;
}


//
// Decode PF/VF from req_id/comp_id format.
//
static inline void pcie_ss_dec_vf_id(
    uint32_t id,
    uint32_t *vf_num,
    bool *vf_active,
    uint32_t *pf_num
)
{
    *vf_num = id >> 4;
    *vf_active = (id >> 3) & 1;
    *pf_num = id & 0x7;
}


// ========================================================================
//
//  DPI-C types shared with SystemVerilog
//
// ========================================================================

typedef struct {
    int tdata_width_bits;
    int tuser_width_bits;
    int max_outstanding_dma_rd_reqs;    // DMA tags must be less than this value
    int max_outstanding_mmio_rd_reqs;   // MMIO tags must be less than this value
    int num_afu_interrupts;
    int num_of_sop;                     // Maximum number of SOPs in one tdata
    int num_of_seg;                     // Maximum number of segments in one tdata
    int max_payload_bytes;              // Maximum size of a payload
    int request_completion_boundary;    // Minimum size of a read completion
} t_ase_pcie_ss_param_cfg;

extern t_ase_pcie_ss_param_cfg pcie_ss_param_cfg;

// Extra configuration (not part of DPI-C), derived from the parameters above

typedef struct {
    uint32_t tlp_hdr_dwords;            // Number of DWORDS in a TLP header
    uint32_t tlp_tdata_dwords;          // Number of DWORDS in the tdata bus
} t_ase_pcie_ss_cfg;

extern t_ase_pcie_ss_cfg pcie_ss_cfg;
    

// ========================================================================
//
//  Public methods
//
// ========================================================================

void pcie_ss_tlp_hdr_reset(t_pcie_ss_hdr_upk *hdr);

void pcie_ss_tlp_payload_reset(
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
);

// Pack the expanded C TLP message into the encoded packed vector
void pcie_ss_tlp_hdr_pack(
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep,
    const t_pcie_ss_hdr_upk *hdr
);

// Unpack the hardware format into a C TLP struct
void pcie_ss_tlp_hdr_unpack(
    t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
);


void pcie_ss_mmio_new_req(const mmio_t *pkt);

const char* pcie_ss_func_fmttype_to_string(uint8_t fmttype);

void fprintf_pcie_ss_hdr(FILE *stream, const t_pcie_ss_hdr_upk *hdr);

void fprintf_pcie_ss_afu_to_host(
    FILE *stream,
    long long cycle,
    bool eop,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
);

void fprintf_pcie_ss_host_to_afu(
    FILE *stream,
    long long cycle,
    bool eop,
    const t_pcie_ss_hdr_upk *hdr,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
);

#endif // _PCIE_SS_TLP_STREAM_H_
