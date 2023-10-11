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
//
// Encoded data mover memory request header:
//
// typedef struct packed {
//     // Byte 31 - Byte 28
//     logic   [31:0]      metadata_l;         // [DW7 31: 0] meta-data field
//
//     // Byte 27 - Byte 24
//     logic   [31:0]      metadata_h;         // [DW6 31: 0] meta-data field
//
//     // Byte 23 - Byte 20
//     logic   [6:0]       rsvd3;              // [DW5 31:25] Reserved
//     logic               mm_mode;            // [DW5 24:24] Memory Mapped mode
//     logic   [4:0]       slot_num;           // [DW5 23:19] Slot Number
//     logic   [3:0]       rsvd4;              // [DW5 18:15] Reserved
//     logic               vf_active;          // [DW5 14:14]
//     logic   [10:0]      vf_num;             // [DW5 13: 3]
//     logic   [2:0]       pf_num;             // [DW5  2: 0]
//
//     // Byte 19 - Byte 16
//     logic   [1:0]       rsvd5;              // [DW4 31:30] Reserved
//     logic               pref_present;       // [DW4 29:29] Prefix Present
//     logic   [4:0]       pref_type;          // [DW4 28:24] Prefix Type
//     logic   [23:0]      pref;               // [DW4 23: 0] Prefix
//
//     // Byte 15 - Byte 12
//     logic   [29:0]      host_addr_m;        // [DW3 31: 2] HostAddress[31:2]
//     logic   [1:0]       PH;                 // [DW3  1: 0]
//
//     // Byte 11 - Byte 8
//     logic   [31:0]      host_addr_h;        // [DW2 31: 0] HostAddress[63:31]
//
//     // Byte 7 - Byte 4
//     logic   [1:0]       host_addr_l;        // [DW1 31:30] HostAddress[1:0]
//     logic   [11:0]      length_h;           // [DW1 29:18] Length[23:12]
//     logic   [1:0]       length_l;           // [DW1 17:16] Length[1:0]
//     logic   [7:0]       tag_l;              // [DW1 15: 8] Tag[7:0]
//     logic   [7:0]       rsvd6;              // [DW1  7: 0] Reserved
//
//     // Byte 3 - Byte 0
//     ReqHdr_FmtType_e    fmt_type;           // [DW0 31:24] Specify the type (read/write) - 8 bits wide
//     logic               tag_h;              // [DW0 23:23] Tag[9]
//     logic   [2:0]       TC;                 // [DW0 22:20]
//     logic               tag_m;              // [DW0 19:19] Tag[8]
//     ReqHdr_Attr_t       attr;               // [DW0 18:10] Attribute Bits - 9 bits wide
//     logic   [9:0]       length_m;           // [DW0  9: 0] Length[11:2]
//
// } PCIe_ReqHdr_t;
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

//
//
// Encoded data mover completion header:
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
//     logic   [23:0]      pref;               // [DW4 23:00] Prefix
//
//     // Byte 15 - Byte 12
//     logic   [9:0]       tag;                // [DW3 31:22] TAG
//     logic               FC;                 // [DW3 21:21] FC
//     logic               rsvd4;              // [DW3 20:20] Reserved
//     logic   [1:0]       length_h;           // [DW3 19:18] length[13:12]
//     logic   [1:0]       length_l;           // [DW3 17:16] length[1:0]
//     logic   [15:0]      low_addr_h;         // [DW3 15: 0] LowerAddress[23:8]
//
//     // Byte 11 - Byte 8
//     logic   [15:0]      rsvd5;              // [DW2 31:16] Reserved
//     logic   [7:0]       rsvd6;              // [DW2 15: 8] Reserved
//     logic   [7:0]       low_addr_l;         // [DW2  7: 0] LowerAddress[7:0]
//
//     // Byte 7 - Byte 4
//     logic   [15:0]      rsvd7;              // [DW1 31:16] Reserved
//     logic   [2:0]       cpl_status;         // [DW1 15:13] Completition Status
//     logic   [12:0]      rsvd8;              // [DW1 12:00] Reserved
//
//     // Byte 3 - Byte 0
//     ReqHdr_FmtType_e    fmt_type;           // [DW0 31:24] Specify the type (completion=4A) - 8 bits wide
//     logic               rsvd9;              // [DW0 23:23] Reserved
//     logic   [2:0]       TC;                 // [DW0 22:20] TC
//     logic               rsvd10;             // [DW0 19:19] Reserved
//     ReqHdr_Attr_t       attr;               // [DW0 18:10] Attribute Bits - 9 bits wide
//     logic   [9:0]       length_m;           // [DW0  9: 0] length[11:2]
//
// } PCIe_CplHdr_t;
//

// Attributes (read/write requests)
typedef struct
{
    bool ln;
    bool th;
    bool td;
    bool ep;
    uint8_t at;
}
t_pcie_ss_hdr_req_attr_upk;

// Header fields used only in requests
typedef struct
{
    uint64_t addr;
    uint8_t last_dw_be;
    uint8_t first_dw_be;
    t_pcie_ss_hdr_req_attr_upk attr;
}
t_pcie_ss_hdr_req_upk;

// Header fields used only in completions
typedef struct
{
    uint16_t comp_id;
    uint8_t cpl_status;
    uint8_t bcm;
    uint16_t byte_count;
    uint32_t low_addr;
    bool fc;
}
t_pcie_ss_hdr_cpl_upk;

// Header fields used only for interrupts
typedef struct
{
    uint16_t vector_num;
}
t_pcie_ss_hdr_intr_upk;

typedef struct
{
    uint64_t metadata;

    uint8_t bar_number;
    uint8_t mm_mode;
    uint8_t slot_num;
    uint8_t vf_active;
    uint16_t vf_num;
    uint8_t pf_num;

    bool pref_present;
    uint8_t pref_type;
    uint32_t pref;

    uint16_t req_id;
    uint16_t tag;

    uint32_t len_bytes;
    uint8_t fmt_type;

    union {
        t_pcie_ss_hdr_req_upk req;
        t_pcie_ss_hdr_cpl_upk cpl;
        t_pcie_ss_hdr_intr_upk intr;
    } u;

    bool dm_mode;         // Data mover mode?
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
    int max_rd_req_bytes;               // Maximum size of a DMA read request (PU)
    int max_wr_payload_bytes;           // Maximum size of a DMA write request (PU)
    int max_dm_rd_req_bytes;            // Maximum size of a DMA read request (DM)
    int max_dm_wr_payload_bytes;        // Maximum size of a DMA write request (DM)
    int request_completion_boundary;    // Minimum size of a read completion
    int ordered_completions;            // Keep completions in order?
    int emulate_tag_mapper;             // Accept duplicate DMA read request tags?

    // ASE currently supports only one active function. Set the default function.
    // This could be overridden by ASE configuration at run time.
    int default_pf_num;
    int default_vf_num;
    int default_vf_active;
} t_ase_pcie_ss_param_cfg;

extern t_ase_pcie_ss_param_cfg pcie_ss_param_cfg;

// Extra configuration (not part of DPI-C), derived from the parameters above

typedef struct {
    uint32_t tlp_hdr_dwords;            // Number of DWORDS in a TLP header
    uint32_t tlp_tdata_dwords;          // Number of DWORDS in the tdata bus
    uint32_t max_any_rd_req_bytes;      // Max of either PU or DM read request
    uint32_t max_any_wr_payload_bytes;  // Max of either PU or DM write request
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
