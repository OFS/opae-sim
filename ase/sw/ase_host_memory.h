// Copyright(c) 2018, Intel Corporation
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
// **************************************************************************

#ifndef _ASE_HOST_MEMORY_H_
#define _ASE_HOST_MEMORY_H_

//
// Definitions for accessing host memory from the FPGA simulator.  Host
// memory address mapping and contents are managed entirely in the application.
// The RTL simulator sends requests to the application for all reads, writes
// and address translation.
//
// The primary reason for handling all memory in the application instead of
// by allocating shared memory buffers with the simulator is to be able
// to map and access an arbitrary location from the FPGA, even if the page
// is already mapped and in use by the application.  It is nearly impossible
// in Linux to reclassify an existing memory page as shared.  Handling all
// accesses on the application side makes shared mapping unnecessary.
//

//
// Requests types that may be sent from simulator to application.
// We define these as const uint64_t instead of enum for alignment
// when passing between 32 bit simulators and 64 bit applications.
//
#define HOST_MEM_REQ_READ 0
#define HOST_MEM_REQ_WRITE 1
#define HOST_MEM_REQ_ATOMIC 2   // Atomic requests are sent in the read queue
typedef uint32_t ase_host_memory_req;

#define HOST_MEM_AT_UNTRANS 0   // Untranslated address (IOVA)
#define HOST_MEM_AT_REQ_TRANS 1 // Translation request
#define HOST_MEM_AT_TRANS 2     // Translated address
typedef uint32_t ase_host_memory_addr_type;

#define HOST_MEM_STATUS_VALID 0
// Reference to illegal address
#define HOST_MEM_STATUS_ILLEGAL 1
// Reference to address that isn't pinned for I/O
#define HOST_MEM_STATUS_NOT_PINNED 2
// Address is pinned but the page is unmapped. This state most likely
// happens when the program unmaps a page that is still pinned.
#define HOST_MEM_STATUS_NOT_MAPPED 3
// Access crosses 4KB boundary
#define HOST_MEM_STATUS_ILLEGAL_4KB 4
typedef uint64_t ase_host_memory_status;

// Maximum size (bytes) of a memory read or write request within
// the simulator. This may be larger than the maximum on a particular
// simulated bus.
#define HOST_MEM_MAX_DATA_SIZE 4096

// Atomic functions, used in read requests
#define HOST_MEM_ATOMIC_OP_FETCH_ADD 1
#define HOST_MEM_ATOMIC_OP_SWAP 2
#define HOST_MEM_ATOMIC_OP_CAS 3

//
// Read request, simulator to application. Also used for atomic updates.
//
typedef struct {
	uint64_t addr;
	ase_host_memory_req req;
	ase_host_memory_addr_type addr_type;
	uint32_t data_bytes;
	uint32_t tag;

	uint32_t pasid;
	uint32_t dummy_pad; // 64 bit alignment

	int32_t afu_idx;	// Emulated AFU index. The FPGA-side emulation
						// will turn this into a PF/VF number.

	// Atomic update payload. Since it is fixed length and small it is sent
	// along with the request. Independent of size, two input functions like
	// compare and swap always store one operand in index 0 and the other in
	// index 1.
	uint64_t atomic_wr_data[2];
	uint8_t	 atomic_func;
} ase_host_memory_read_req;

//
// Read response, application to simulator.
//
typedef struct {
	// Simulated host physical address
	uint64_t pa;
	// Virtual address in application space.  We store this as
	// a uint64_t so the size is consistent even in 32 bit simulators.
	uint64_t va;

	// Size of the payload that follows this response in the stream
	uint32_t data_bytes;
	uint32_t tag;

	int32_t afu_idx;	// Emulated AFU index. The FPGA-side emulation
						// will turn this into a PF/VF number.

	// Does the response hold valid data?
	ase_host_memory_status status;
} ase_host_memory_read_rsp;

//
// Write request, simulator to application.
//
typedef struct {
	uint64_t addr;
	ase_host_memory_req req;
	ase_host_memory_addr_type addr_type;

	uint32_t pasid;

	// Byte range (PCIe-style 4 bit first byte/last byte enable mask.)
	// fbe and lbe are ignored when byte_n is 0. Data_bytes must be a
	// multiple of 4 when byte_en is set.
	uint8_t byte_en;
	uint8_t first_be;        // 4 bit byte mask in first DWORD
	uint8_t last_be;         // 4 bit byte mask in last DWORD
	uint8_t rsvd;

	uint32_t data_bytes;     // Size of the data payload the follows in the message stream

	int32_t afu_idx;         // Emulated AFU index. The FPGA-side emulation
	                         // will turn this into a PF/VF number.
} ase_host_memory_write_req;

//
// Write response, application to simulator.
//
typedef struct {
	// Simulated host physical address
	uint64_t pa;
	// Virtual address in application space.  We store this as
	// a uint64_t so the size is consistent even in 32 bit simulators.
	uint64_t va;

	int32_t afu_idx;         // Emulated AFU index. The FPGA-side emulation
	                         // will turn this into a PF/VF number.

	// Was the request to a valid address?
	ase_host_memory_status status;
} ase_host_memory_write_rsp;


#ifndef SIM_SIDE

extern bool ase_pt_enable_debug;

// Pin a page at specified virtual address. Allocates and returns
// an IOVA.
int ase_host_memory_pin(int32_t afu_idx, void *va, uint64_t *iova, uint64_t length);
// Unpin the page at IOVA.
int ase_host_memory_unpin(int32_t afu_idx, uint64_t iova, uint64_t length);

// Translate from simulated IOVA address space.  By setting "lock"
// in the request, the internal page table lock is not released on
// return. This allows a caller to be sure that a page will remain
// in the table long enough to access the data to which pa points.
// Callers using "lock" must call ase_host_memory_unlock() to
// release the page table lock and avoid deadlocks.
uint64_t ase_host_memory_iova_to_va(int32_t afu_idx, uint64_t iova, bool lock);
void ase_host_memory_unlock(void);

// Translate a VA to simulated physical address space and add
// the address to the PA->VA tracking table. This is used by the
// PCIe ATS emulation. It is not a translation to IOVA!
// The page length is an output.
uint64_t ase_host_memory_va_to_pa(uint64_t va, uint64_t *length);
// Similar to IOVA->VA but from the simulated PA space to VA.
uint64_t ase_host_memory_pa_to_va(uint64_t pa, bool lock);

// Return the size of the memory page at "va", in bytes. If no memory is mapped
// at the address return 0.
//
// Returns -1 on fatal error.
int64_t ase_host_memory_va_page_len(uint64_t va);

// Invalidate virtual range, removing it from the PA->VA tracking table.
void ase_host_memory_inval_va_range(uint64_t va, uint64_t length);

// Initialize/terminate page address translation.
int ase_host_memory_initialize(void);
void ase_host_memory_terminate(void);
void ase_host_memory_terminate_afu(int32_t afu_idx);

#else

void memline_addr_error(const char *access_type,
						ase_host_memory_status status,
						uint64_t pa, uint64_t va);

#endif // not SIM_SIDE

#endif // _ASE_HOST_MEMORY_H_
