// Copyright(c) 2018-2023, Intel Corporation
//
// Redistribution  and	use	 in source	and	 binary	 forms,	 with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of	 source code  must retain the  above copyright notice,
//	 this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//	 this list of conditions and the following disclaimer in the documentation
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

//
// Manage simulated host memory.  This code runs on the application side.
// The simulator makes requests that are serviced inside the application,
// thus allowing the application to update share any page at any point
// in a run.
//
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <assert.h>
#include <pthread.h>

#include <opae/mem_alloc.h>
#include "ase_common.h"
#include "ase_host_memory.h"

#define KB 1024
#define MB (1024 * KB)
#define GB (1024UL * MB)

static pthread_mutex_t ase_pt_lock = PTHREAD_MUTEX_INITIALIZER;
bool ase_pt_enable_debug = 0;

/*
 * Tracking structures for IOVA-addressed pinned pages. ASE uses the same
 * code as the OPAE SDK's vfio library to manage the IOVA address space.
 * The ase_iova_pt_root table maps IOVA back to process VAs, since ASE
 * uses its virtual address space to read and write memory. On real
 * hardware, the table would map IOVA to PA. 
 */
static uint64_t *ase_iova_pt_root;
static struct mem_alloc iova_mem_alloc;

/*
 * Tracking for PA-addressed pages, used for PCIe ATS SVM emulation.
 * No struct mem_alloc is needed because ASE uses
 * ase_host_memory_gen_xor_mask() below for assigning PAs.
 */
static uint64_t *ase_pa_pt_root;

/*
 * Cache the most recent result of ase_host_memory_va_page_len() because
 * walking /proc/self/smaps is slow. A contiguous range of pages has
 * a single entry in smaps.
 */
static bool cached_page_len_valid;
static unsigned long long cached_page_range_start, cached_page_range_end;
static unsigned cached_page_len;

STATIC int ase_pt_length_to_level(uint64_t length);
static uint64_t ase_pt_level_to_bit_idx(int pt_level);
static void ase_pt_delete_tree(uint64_t *pt, int pt_level);
static uint64_t ase_pt_lookup_addr(uint64_t pa, uint64_t *pt_root, int *pt_level);
static int ase_pt_pin_page(uint64_t va, uint64_t iova, uint64_t *pt_root, int pt_level);
static int ase_pt_unpin_page(uint64_t iova, uint64_t *pt_root, int pt_level);


/*
 * Pin a page at specified virtual address. Allocates and returns the
 * corresponding IOVA.
 */
int ase_host_memory_pin(void *va, uint64_t *iova, uint64_t length)
{
	int status = -1;

	if (pthread_mutex_lock(&ase_pt_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain the lock !\n");
		return -1;
	}

	// Map buffer length to a level in the page table.
	int pt_level = ase_pt_length_to_level(length);
	if (pt_level == -1)
		goto err_unlock;

	// Pick an IOVA
	status = mem_alloc_get(&iova_mem_alloc, iova, length);
	if (status)
		goto err_unlock;

	status = ase_pt_pin_page((uint64_t)va, *iova, ase_iova_pt_root, pt_level);
	if (status)
		goto err_unlock;

	ase_host_memory_unlock();
	note_pinned_page((uint64_t)va, *iova, length);
	return 0;

err_unlock:
	ase_host_memory_unlock();
	return status;
}


/*
 * Unpin the page at iova.
 */
int ase_host_memory_unpin(uint64_t iova, uint64_t length)
{
	int status = 0;

	if (pthread_mutex_lock(&ase_pt_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain lock !\n");
		return -1;
	}

	mem_alloc_put(&iova_mem_alloc, iova);

	if (ase_iova_pt_root != NULL) {
		int pt_level = ase_pt_length_to_level(length);
		if (pt_level == -1)
			return -1;

		status = ase_pt_unpin_page(iova, ase_iova_pt_root, pt_level);
		if (status < 0)
			ASE_ERR("Error removing page from IOVA page table (%d)\n", status);
	}

	ase_host_memory_unlock();
	note_unpinned_page(iova, length);
	return status;
}

/*
 * Translate from simulated IOVA address space. Optionally hold the lock
 * after translation so that the buffer remains pinned. Callers that set
 * "lock" must call ase_host_memory_unlock() or subsequent calls to
 * ase_host_memory functions will deadlock.
 */
uint64_t ase_host_memory_iova_to_va(uint64_t iova, bool lock)
{
	if (pthread_mutex_lock(&ase_pt_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain lock !\n");
		return 0;
	}

	// Is the page pinned?
	int pt_level;
	uint64_t va;
	va = ase_pt_lookup_addr(iova, ase_iova_pt_root, &pt_level);

	if (!va)
	{
		ase_host_memory_unlock();
		return 0;
	}

	if (!lock)
		ase_host_memory_unlock();

	// Return VA: page base and offset from IOVA
	uint64_t offset = iova & ((UINT64_C(1) << ase_pt_level_to_bit_idx(pt_level)) - 1);
	return va | offset;
}


/*
 * Generate an XOR mask that will be used to map between virtual and physical
 * addresses. An XOR is used so that it is easy to map both directions: VA->PA
 * and PA->VA without building tables in each direction. We still build a PA->VA
 * table for two reasons: the page size at PA is unknown and a PA->VA table
 * confirms that a page has been exposed through the IOMMU. No VA->PA table is
 * needed.
 */
static inline uint64_t ase_host_memory_gen_xor_mask(int pt_level)
{
	// CCI-P (and our processors) have 48 bit byte-level addresses.
	// The mask here inverts all but the high 48th bit. We could legally
	// invert that bit too, except that it causes problems when simulating
	// old architectures such as the Broadwell integrated Xeon+FPGA, which
	// use smaller physical address ranges.
	return UINT64_C(0x7fffffffffff) & (~UINT64_C(0) << ase_pt_level_to_bit_idx(pt_level));
}


/*
 * Return the size of the memory page at "va", in bytes. If no memory is mapped
 * at the address return 0.
 *
 * Returns -1 on fatal error.
 */
int64_t ase_host_memory_va_page_len(uint64_t va)
{
	int64_t page_size;
	char line[4096];

	if (cached_page_len_valid && (cached_page_range_start <= va) && (va < cached_page_range_end))
		return cached_page_len;

	page_size = 0;

	FILE *f = fopen("/proc/self/smaps", "r");
	if (f == NULL)
	{
		return -1;
	}

	while (fgets(line, sizeof(line), f))
	{
		unsigned long long start, end;
		char* tmp0;
		char* tmp1;

		// Range entries begin with <start va>-<end va>
		start = strtoll(line, &tmp0, 16);
		// Was a number found and is the next character a dash?
		if ((tmp0 == line) || (*tmp0 != '-'))
		{
			// No -- not a range
			continue;
		}

		end = strtoll(++tmp0, &tmp1, 16);
		// Keep searching if not a number or the address isn't in range.
		if ((tmp0 == tmp1) || (start > va) || (end <= va))
		{
			continue;
		}

		while (fgets(line, sizeof(line), f))
		{
			// Look for KernelPageSize
			unsigned page_kb;
			int ret = sscanf(line, "KernelPageSize: %u kB", &page_kb);
			if (ret == 0)
				continue;

			fclose(f);

			if (ret < 1 || page_kb == 0) {
				return -1;
			}

			// page_kb is reported in kB. Convert to bytes.
			int64_t sz = -1;
			if (page_kb >= 1048576)
				sz = GB;
			else if (page_kb >= 2048)
				sz = 2 * MB;
			else if (page_kb >= 4)
				sz = 4 * KB;
			else
				return -1;

			cached_page_len_valid = true;
			cached_page_range_start = start;
			cached_page_range_end = end;
			cached_page_len = sz;

			return sz;
		}
	}

	// We couldn't find an entry for this addr in smaps.
	fclose(f);
	return 0;
}


/*
 * Translate a VA to simulated physical address space and add
 * the address to the PA->VA tracking table. This is used by the
 * PCIe ATS emulation. It is not a translation to IOVA!
 */
uint64_t ase_host_memory_va_to_pa(uint64_t va, uint64_t *length)
{
	uint64_t pa = 0;

	if (pthread_mutex_lock(&ase_pt_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain lock !\n");
		return INT64_C(-1);
	}

	if (!va)
		goto err_unlock;

	int64_t page_len = ase_host_memory_va_page_len(va);
	if (page_len == -1)
		goto err_unlock;
	if (page_len == 0)
		goto out_unlock;

	int pt_level = ase_pt_length_to_level(page_len);
	pa = va ^ ase_host_memory_gen_xor_mask(pt_level);

	// Is the address already in the table?
	uint64_t cur_table_va;
	int cur_pt_level;
	cur_table_va = ase_pt_lookup_addr(pa, ase_pa_pt_root, &cur_pt_level);
	if (cur_table_va)
	{
		if (cur_table_va != va || cur_pt_level != pt_level)
			ASE_ERR("Two mappings in PA page table for VA 0x%016" PRIx64 "\n");
		goto out_unlock;
	}

	// Add PA->VA mapping to the table
	int status = ase_pt_pin_page(va, pa, ase_pa_pt_root, pt_level);
	if (status)
		goto err_unlock;

out_unlock:
	if (length)
		*length = page_len;
	ase_host_memory_unlock();
	return pa;

err_unlock:
	ase_host_memory_unlock();
	return INT64_C(-1);
}


uint64_t ase_host_memory_pa_to_va(uint64_t pa, bool lock)
{
	if (pthread_mutex_lock(&ase_pt_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain lock !\n");
		return 0;
	}

	// Is the page pinned?
	int pt_level;
	uint64_t va;
	va = ase_pt_lookup_addr(pa, ase_pa_pt_root, &pt_level);

	if (!va)
	{
		ase_host_memory_unlock();
		return 0;
	}

	if (!lock)
		ase_host_memory_unlock();

	// Return VA: page base and offset from IOVA
	uint64_t offset = pa & ((UINT64_C(1) << ase_pt_level_to_bit_idx(pt_level)) - 1);
	return va | offset;
}


void ase_host_memory_inval_va_range(uint64_t va, uint64_t length)
{
	uint64_t va_end = va + length;

	if (pthread_mutex_lock(&ase_pt_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain lock !\n");
		return;
	}

	// Invalidate page by page
	while (va < va_end)
	{
		int64_t page_len = ase_host_memory_va_page_len(va);
		if (page_len <= 0)
			break;

		// Start address of current page
		va = va & ~(page_len - 1);

		// Drop reverse page entry PA->VA
		int pt_level = ase_pt_length_to_level(page_len);
		uint64_t pa = va ^ ase_host_memory_gen_xor_mask(pt_level);
		ase_pt_unpin_page(pa, ase_pa_pt_root, pt_level);

		va += page_len;
	}

	ase_host_memory_unlock();
}


void ase_host_memory_unlock(void)
{
	cached_page_len_valid = false;

	if (pthread_mutex_unlock(&ase_pt_lock))
		ASE_ERR("pthread_mutex_lock could not unlock !\n");
}


int ase_host_memory_initialize(void)
{
	// Turn on debugging messages when the environment variable ASE_PT_DBG
	// is defined.
	ase_pt_enable_debug = ase_checkenv("ASE_PT_DBG");

	if (ase_iova_pt_root == NULL) {
		ase_iova_pt_root = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
		if (ase_iova_pt_root == MAP_FAILED) {
			ASE_ERR("Simulated IOVA page table out of memory!\n");
			return -1;
		}
		ase_memset(ase_iova_pt_root, 0, 4096);
	}

	if (ase_pa_pt_root == NULL) {
		ase_pa_pt_root = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
		if (ase_pa_pt_root == MAP_FAILED) {
			ASE_ERR("Simulated PA page table out of memory!\n");
			return -1;
		}
		ase_memset(ase_pa_pt_root, 0, 4096);
	}

	mem_alloc_init(&iova_mem_alloc);
	// Initialize IOVA free space with values that are similar to HW
	mem_alloc_add_free(&iova_mem_alloc, 0, UINT64_C(0xfee00000));
	mem_alloc_add_free(&iova_mem_alloc, UINT64_C(0xfef00000), UINT64_C(0x1ffffff01100000));

	return 0;
}


void ase_host_memory_terminate(void)
{
	if (pthread_mutex_lock(&ase_pt_lock))
		ASE_ERR("pthread_mutex_lock could not attain the lock !\n");

	ase_pt_delete_tree(ase_iova_pt_root, 3);
	ase_iova_pt_root = NULL;
	mem_alloc_destroy(&iova_mem_alloc);

	ase_pt_delete_tree(ase_pa_pt_root, 3);
	ase_pa_pt_root = NULL;

	ase_host_memory_unlock();
}


// ========================================================================
//
//	Maintain a simulated page table in order to track pinned pages.
//
// ========================================================================

/*
 * The page table maps a physical space, either simulated PAs or IOVAs
 * to user-space virtual addresses. This is the opposite of a normal
 * page table. ASE is a user-space application and ultimately reads and
 * writes memory with VAs.
 *
 * A pointer value with the high bit set indicates a terminal page,
 * pointing to a real user-space page outside the page table.
 */

/*
 * Mask to extract just the address from a page table entry, dropping flags.
 * The top bit is used to signal pages larger than 4KB and the low 12 bits,
 * which would be an offset into a 4KB page, are reserved for flags and
 * reference counts.
 */
#define ASE_PT_ADDR_MASK UINT64_C(0x7ffffffffffff000)

/*
 * Test whether page table entry is terminal -- a reference to a user
 * memory page instead of another level in the page table.
 */
static inline bool ase_pt_entry_is_terminal(uint64_t *pt)
{
	return (uint64_t)pt >> 63;
}

/*
 * Increment the reference counter for a huge page entry and return
 * the new count.
 */
static inline uint8_t ase_pt_incr_refcnt(uint64_t *pt)
{
	if (*pt == 0) {
		// Not set yet -- initialize with high bit set and a refcount of 1
		*pt = ((uint64_t)1 << 63) | 1;
	} else {
		// Increment the refcount (stored in the low 8 bits)
		uint64_t v = *pt;

		// Must already be a terminal entry
		assert(ase_pt_entry_is_terminal((uint64_t *)*pt));

		// Preserve all but the low 8 bits and add 1 to the low 8 bits
		v = (v & ~(uint64_t)0xff) | ((uint8_t)v + 1);
		*pt = v;
	}

	uint8_t c = *pt;
	assert(c != 0);

	return c;
}

/*
 * Decrement the reference counter for a huge page entry and return
 * the new count.
 */
static inline uint8_t ase_pt_decr_refcnt(uint64_t *pt)
{
	// Must already be a terminal entry
	assert(ase_pt_entry_is_terminal((uint64_t *)*pt));

	// Decrement the refcount (stored in the low 8 bits)
	uint64_t v = *pt;

	// Preserve all but the low 8 bits and add 1 to the low 8 bits
	v = (v & ~(uint64_t)0xff) | ((uint8_t)v - 1);
	*pt = v;

	uint8_t c = *pt;
	assert(c != 0xff);

	return c;
}

/*
 * Get the reference count for a page pointer.
 */
static inline uint8_t ase_pt_get_refcnt(uint64_t pt)
{
	return pt & 0xff;
}

/*
 * Set the address in a page table entry, preserving metadata.
 */
static inline void ase_pt_set_addr(uint64_t *pt, uint64_t addr)
{
	*pt = (addr & ASE_PT_ADDR_MASK) | (*pt & ~ASE_PT_ADDR_MASK);
}

static inline uint64_t ase_pt_get_addr(uint64_t pt_entry)
{
	return pt_entry & ASE_PT_ADDR_MASK;
}

/*
 * Page size to level in the table. Level 3 is the root, though we never
 * return 3 since the hardware won't allocated 512GB huge pages.
 */
STATIC int ase_pt_length_to_level(uint64_t length)
{
	int pt_level;

	if (length > 2 * MB)
		pt_level = (length == GB) ? 2 : -1;
	else if (length > 4 * KB)
		pt_level = (length <= 2 * MB) ? 1 : -1;
	else
		pt_level = 0;

	return pt_level;
}

/*
 * Return the bit index of the low bit of an address corresponding to
 * pt_level. All address bits lower than the returned index will be
 * offsets into the page.
 */
static inline uint64_t ase_pt_level_to_bit_idx(int pt_level)
{
	// Level 0 is 4KB pages, so 12 bits.
	uint64_t idx = 12;

	// Each level up adds 9 bits, corresponding to 512 entries in each
	// page table level in the tree.
	idx += pt_level * 9;

	return idx;
}

/*
 * Index of a 512 entry set of page pointers in the table, given a level.
 */
static inline int ase_pt_idx(uint64_t iova, int pt_level)
{
	// The low 12 bits are the 4KB page offset. Groups of 9 bits above that
	// correspond to increasing levels in the page table hierarchy.
	assert(pt_level <= 3);
	return 0x1ff & (iova >> ase_pt_level_to_bit_idx(pt_level));
}

static const char* ase_pt_name(uint64_t *pt_root)
{
	if (pt_root == ase_iova_pt_root)
		return "IOVA";
	if (pt_root == ase_pa_pt_root)
		return "PA";
	return "UNKNOWN";
}

/*
 * Dump the page table for debugging.
 */
static void ase_pt_dump(uint64_t *pt, uint64_t iova, int pt_level)
{
	if (pt == NULL)
		return;

	if (ase_pt_entry_is_terminal(pt)) {
		printf("  0x%016" PRIx64 " -> 0x%016" PRIx64 "	  %ld  (refcnt %d)\n",
		       iova, ase_pt_get_addr((uint64_t)pt),
		       4096 * (UINT64_C(1) << (9 * (pt_level + 1))),
		       ase_pt_get_refcnt((uint64_t)pt));
		return;
	}

	int idx;
	for (idx = 0; idx < 512; idx++) {
		if (pt_level > 0) {
			ase_pt_dump((uint64_t *)pt[idx],
				    iova | ((uint64_t)idx << ase_pt_level_to_bit_idx(pt_level)),
				    pt_level - 1);
		} else {
			uint8_t refcnt = ase_pt_get_refcnt(pt[idx]);
			if (refcnt) {
				printf("  0x%016" PRIx64 " -> 0x%016" PRIx64 "	  4096	(refcnt %d)\n",
				       iova | (idx << 12), ase_pt_get_addr((uint64_t)pt[idx]), refcnt);
			}
		}
	}
}


/*
 * Delete a sub-tree in the table.
 */
static void ase_pt_delete_tree(uint64_t *pt, int pt_level)
{
	if ((pt == NULL) || ase_pt_entry_is_terminal(pt))
		return;

	if (pt_level) {
		// Drop sub-trees and then release this node.
		int idx;
		for (idx = 0; idx < 512; idx++) {
			ase_pt_delete_tree((uint64_t *)pt[idx], pt_level - 1);
		}
	}

	munmap(pt, 4096);
}


/*
 * Return mapped address stored in the table or NULL if not found.
 * The level at which it is found is stored *pt_level.
 */
static uint64_t ase_pt_lookup_addr(uint64_t pa, uint64_t *pt_root, int *pt_level)
{
	*pt_level = -1;

	int level = 3;
	uint64_t *pt = pt_root;

	while (level > 0) {
		if (pt == NULL) {
			// Not found
			return 0;
		}

		// Walk down to the next level. Unlike a normal page table, addresses
		// here are simple virtual pointers. We can do this since the table
		// isn't actually translating -- it is simply indicating whether a
		// physical address is pinned.
		pt = (uint64_t *) pt[ase_pt_idx(pa, level)];
		if (ase_pt_entry_is_terminal(pt)) {
			*pt_level = level;
			return ase_pt_get_addr((uint64_t)pt);
		}

		level -= 1;
	}

	// The last level mapping is a simple 512 entry bit vector -- not a set
	// of pointers. We do this to save space since the table only has to
	// indicate whether a page is valid.
	int idx = ase_pt_idx(pa, 0);
	if (pt && ase_pt_get_refcnt(pt[idx])) {
		*pt_level = 0;
		return ase_pt_get_addr(pt[idx]);
	}

	// Not found
	if (ase_pt_enable_debug) {
		printf("\nASE simulated page table (%s 0x%" PRIx64 " not found):\n",
		       ase_pt_name(pt_root), pa);
		ase_pt_dump(pt_root, 0, 3);
		printf("\n");
	}

	return 0;
}

static int ase_pt_pin_page(uint64_t va, uint64_t iova, uint64_t *pt_root, int pt_level)
{
	assert((pt_level >= 0) && (pt_level < 3));

	uint64_t length = 4096 * (UINT64_C(1) << (9 * pt_level));

	ASE_MSG("Add pinned page VA 0x%" PRIx64 ", %s 0x%" PRIx64 ", level %d\n", va, ase_pt_name(pt_root), iova, pt_level);

	int idx;
	int level = 3;
	uint64_t *pt = pt_root;

	while (level != pt_level) {
		idx = ase_pt_idx(iova, level);
		if (pt[idx] == 0) {
			pt[idx] = (uint64_t)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
						 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
			if (pt[idx] == (uint64_t)MAP_FAILED) {
				ASE_ERR("Simulated page table out of memory!\n");
				pt[idx] = 0;
				return -1;
			}

			ase_memset((void *)pt[idx], 0, (level > 1) ? 4096 : 512);
		}

		if (ase_pt_entry_is_terminal(pt)) {
			ASE_ERR("Attempt to map smaller page inside existing huge page\n");
			return -1;
		}

		pt = (uint64_t *) pt[idx];
		level -= 1;
	}

	idx = ase_pt_idx(iova, level);
	if (level) {
		if (!ase_pt_entry_is_terminal((uint64_t *)pt[idx]) && (pt[idx] != 0)) {
			// Smaller pages in the same address range are already pinned.
			// What should we do? mmap() allows overwriting existing
			// mappings, so we behave like it for now.
			ase_pt_delete_tree((uint64_t *)pt[idx], level - 1);
			pt[idx] = 0;
		}
	}

	ase_pt_incr_refcnt(&pt[idx]);
	ase_pt_set_addr(&pt[idx], va);

	if (ase_pt_enable_debug) {
		printf("\nASE simulated page table (pinned VA 0x%" PRIx64 ", %s 0x%" PRIx64 "):\n",
		       va, ase_pt_name(pt_root), iova);
		ase_pt_dump(pt_root, 0, 3);
		printf("\n");
	}

	return 0;
}

static int ase_pt_unpin_page(uint64_t iova, uint64_t *pt_root, int pt_level)
{
	assert((pt_level >= 0) && (pt_level < 3));

	ASE_MSG("Remove pinned page %s 0x%" PRIx64 ", level %d\n", ase_pt_name(pt_root), iova, pt_level);

	int idx;
	int level = 3;
	uint64_t *pt = pt_root;
	uint64_t length = 4096 * (UINT64_C(1) << (9 * pt_level));

	while (level != pt_level) {
		idx = ase_pt_idx(iova, level);
		if (pt[idx] == 0) {
			// Attempt to unpin non-existent page
			return -1;
		}

		if (ase_pt_entry_is_terminal(pt)) {
			// Attempt to unpin smaller page inside existing huge page
			return -1;
		}

		pt = (uint64_t *) pt[idx];
		level -= 1;
	}

	idx = ase_pt_idx(iova, level);
	if (level) {
		// Drop a huge page
		if (!ase_pt_entry_is_terminal((uint64_t *)pt[idx])) {
			// Attempt to unpin non-existent page
			return -1;
		}
		if (ase_pt_decr_refcnt(&pt[idx]) == 0) {
			// Reference count is 0. Clear the page.
			pt[idx] = 0;
		}
	} else if (pt) {
		// Drop a 4KB page
		if (!ase_pt_entry_is_terminal((uint64_t *)pt[idx])) {
			// Attempt to unpin non-existent page
			return -1;
		}
		ase_pt_decr_refcnt(&pt[idx]);
	}

	if (ase_pt_enable_debug) {
		printf("\nASE simulated page table (unpinned %s 0x%" PRIx64 "):\n",
		       ase_pt_name(pt_root), iova);
		ase_pt_dump(pt_root, 0, 3);
		printf("\n");
	}

	return 0;
}
