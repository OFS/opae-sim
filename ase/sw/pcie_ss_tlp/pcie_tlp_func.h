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

#ifndef _PCIE_TLP_FUNC_H_
#define _PCIE_TLP_FUNC_H_

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

#endif // _PCIE_TLP_FUNC_H_
