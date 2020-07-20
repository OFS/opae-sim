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

#ifndef _PCIE_TLP_STREAM_H_
#define _PCIE_TLP_STREAM_H_

#include "ase_common.h"

// ========================================================================
//
//  PCIe TLP header types
//
// ========================================================================

//
// The first DWORD in the TLP header defines the message class (fmttype)
// and length.
//
// The SystemVerilog representation:
//     1st DW in TLP header
//     typedef struct packed {
//        logic        rsvd0;    // [31]
//        logic [6:0]  fmttype;  // [30:24]
//        logic        rsvd1;    // [23]
//        logic [2:0]  tc;       // [22:20]
//        logic [2:0]  rsvd2;    // [19:17]
//        logic        th;       // [16]
//        logic        td;       // [15]
//        logic        ep;       // [14]
//        logic [1:0]  attr;     // [13:12]
//        logic [1:0]  rsvd3;    // [11:10]
//        logic [9:0]  length;   // [ 9: 0]
//     } t_tlp_hdr_dw0;
//

// Unpacked (no bit fields) DW0 and functions to pack/unpack it to a uint32_t.
typedef struct
{
    uint8_t fmttype;
    uint8_t tc;
    uint8_t th;
    uint8_t td;
    uint8_t ep;
    uint8_t attr;
    uint16_t length;
}
t_tlp_hdr_dw0_upk;


//
// The OFS FIM represents TLP message types as different structs. The code here
// merges them into a single union type. The C code also breaks bit fields into
// individual byte-granularity types since the C standard doesn't guarantee
// bit-level layout. A pack call is required before forwarding to simulated
// hardware and an unpack is required when receiving a message.
//
// Memory request header, SystemVerilog representation:
//
//     typedef struct packed {
//        t_tlp_hdr_dw0 dw0;          // [DW3 31: 0]
//        logic [15:0]  requester_id; // [DW2 31:16]
//        logic [7:0]   tag;          // [DW2 15: 8]
//        logic [3:0]   last_be;      // [DW2  7: 4]
//        logic [3:0]   first_be;     // [DW2  3: 0]
//        logic [31:0]  addr;         // [DW1 31: 0] MWr64/MRd64:address[63:32]; MWr32/MRd32:address[31:0]
//        logic [31:0]  lsb_addr;     // [DW0 31: 0] MWr64/MRd64:address[31:0];  MWr32/MRd32:rsvd
//     } t_tlp_mem_req_hdr;
//
// Completion header, SystemVerilog representation:
//
//     typedef struct packed {
//        t_tlp_hdr_dw0 dw0;          // [DW3 31: 0]
//        logic [15:0]  completer_id; // [DW2 31:16]
//        logic [2:0]   status;       // [DW2 15:13]
//        logic         bcm;          // [DW2 12]
//        logic [11:0]  byte_count;   // [DW2 11: 0]
//        logic [15:0]  requester_id; // [DW1 31:16]
//        logic [7:0]   tag;          // [DW1 15: 8]
//        logic         rsvd0;        // [DW1  7]
//        logic [6:0]   lower_addr;   // [DW1  6: 0]
//        logic [31:0]  rsvd1;        // [DW0 31: 0]
//     } t_tlp_cpl_hdr;
//

// Memory request-specific fields.
typedef struct {
    uint16_t requester_id;
    uint8_t tag;
    uint8_t last_be;
    uint8_t first_be;
    // Unpacked representation always stores address as a 64 bit value.
    // It will be remapped to 32 bit headers when needed before being
    // passed to hardware.
    uint64_t addr;
} t_tlp_mem_req_hdr_upk;

// Completion-specific fields.
typedef struct {
    uint16_t completer_id;
    uint8_t status;
    uint8_t bcm;
    uint16_t byte_count;
    uint16_t requester_id;
    uint8_t tag;
    uint8_t lower_addr;
} t_tlp_cpl_hdr_upk;

// Unpacked (no bit fields) memory request header and functions to
// pack/unpack it to a vector of uint32_t.
typedef struct {
    t_tlp_hdr_dw0_upk dw0;
    union {
        t_tlp_mem_req_hdr_upk mem;
        t_tlp_cpl_hdr_upk cpl;
    } u;
} t_tlp_hdr_upk;


// Tom Torfs binary literal to integer conversion macro
#define B8__(x) ((x&0x0000000FLU)?1:0) \
    +((x&0x000000F0LU)?2:0)            \
    +((x&0x00000F00LU)?4:0)            \
    +((x&0x0000F000LU)?8:0)            \
    +((x&0x000F0000LU)?16:0)           \
    +((x&0x00F00000LU)?32:0)           \
    +((x&0x0F000000LU)?64:0)           \
    +((x&0xF0000000LU)?128:0)
#define HEX__(n) 0x##n##LU
// Up to 8 bit binary constants
#define B8(d) ((unsigned char)B8__(HEX__(d)))

typedef enum 
{
    PCIE_TYPE_CPL = B8(01010),
    PCIE_TYPE_MEM_RW = B8(00000)
}
t_pcie_type_enum;

typedef enum
{
    PCIE_FMTTYPE_MEM_READ32   = B8(0000000),
    PCIE_FMTTYPE_MEM_READ64   = B8(0100000),
    PCIE_FMTTYPE_MEM_WRITE32  = B8(1000000),
    PCIE_FMTTYPE_MEM_WRITE64  = B8(1100000),
    PCIE_FMTTYPE_CFG_WRITE    = B8(1000100),
    PCIE_FMTTYPE_CPL          = B8(0001010),
    PCIE_FMTTYPE_CPLD         = B8(1001010),
    PCIE_FMTTYPE_SWAP32       = B8(1001101),
    PCIE_FMTTYPE_SWAP64       = B8(1101101),
    PCIE_FMTTYPE_CAS32        = B8(1001110),
    PCIE_FMTTYPE_CAS64        = B8(1101110)
}
t_pcie_fmttype_enum;


static inline bool tlp_func_is_addr32(uint8_t fmttype)
{
    return ((fmttype & 32) == 0);
}

static inline bool tlp_func_is_addr64(uint8_t fmttype)
{
    return !tlp_func_is_addr32(fmttype);
}

static inline bool tlp_func_has_data(uint8_t fmttype)
{
    return (fmttype & 64);
}

static inline bool tlp_func_is_completion(uint8_t fmttype)
{
    return (fmttype & 0x1f) == PCIE_TYPE_CPL;
}

static inline bool tlp_func_is_mem_req(uint8_t fmttype)
{
    return (fmttype & 0x1f) == PCIE_TYPE_MEM_RW;
}

static inline bool tlp_func_is_mem_req64(uint8_t fmttype)
{
    return (tlp_func_is_mem_req(fmttype) && tlp_func_is_addr64(fmttype));
}

static inline bool tlp_func_is_mem_req32(uint8_t fmttype)
{
    return (tlp_func_is_mem_req(fmttype) && tlp_func_is_addr32(fmttype));
}

static inline bool tlp_func_is_mwr_req(uint8_t fmttype)
{
    return (tlp_func_is_mem_req(fmttype) && tlp_func_has_data(fmttype));
}

static inline bool tlp_func_is_mrd_req(uint8_t fmttype)
{
    return (tlp_func_is_mem_req(fmttype) && !tlp_func_has_data(fmttype));
}


// ========================================================================
//
//  DPI-C types shared with SystemVerilog
//
// ========================================================================

typedef struct {
    int num_tlp_channels;
    int max_outstanding_dma_rd_reqs;    // DMA tags must be less than this value
    int max_outstanding_mmio_rd_reqs;   // MMIO tags must be less than this value
    int num_afu_interrupts;
    int max_payload_bytes;              // Maximum size of a payload
    int request_completion_boundary;    // Minimum size of a read completion
    int channel_payload_bytes;
} t_ase_axis_param_cfg;

//
// Single-bit fields are encoded as "uint8_t" for easier handling in C.
// Only the low bit will be used.
//

// Different simulators use different endianness on vectors.
// 

// Generic single-channel format, either AFU->host or host->AFU.
typedef struct {
    svBitVecVal payload[8];
    svBitVecVal hdr[4];
    uint8_t irq_id;    // Used only by AFU->host when afu_irq is set in tuser
    uint8_t eop;
    uint8_t sop;
    uint8_t valid;
} t_ase_axis_pcie_tdata;

// Only the fields ASE simulates are defined. Any others default to 0.
typedef struct {
    uint8_t mmio_req;
} t_ase_axis_pcie_rx_tuser;

// Only the fields ASE simulates are defined. Any others default to 0.
typedef struct {
    uint8_t afu_irq;
} t_ase_axis_pcie_tx_tuser;

// Interrupt response
typedef struct {
    short int rid;
    uint8_t irq_id;
    uint8_t tvalid;
} t_ase_axis_pcie_irq_rsp;


// ========================================================================
//
//  Public methods
//
// ========================================================================

void pcie_mmio_new_req(const mmio_t *pkt);

const char* tlp_func_fmttype_to_string(uint8_t fmttype);

void fprintf_tlp_hdr(FILE *stream, const t_tlp_hdr_upk *hdr);

void fprintf_tlp_afu_to_host(
    FILE *stream,
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_tx_tuser *tuser
);

void fprintf_tlp_host_to_afu(
    FILE *stream,
    long long cycle,
    int ch,
    const t_tlp_hdr_upk *hdr,
    const t_ase_axis_pcie_tdata *tdata,
    const t_ase_axis_pcie_rx_tuser *tuser
);

#endif // _PCIE_TLP_STREAM_H_
