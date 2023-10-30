// Copyright(c) 2023, Intel Corporation
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
// Support functions for PCIe address translation service emulation.
//

#include <byteswap.h>
#include <stdio.h>
#include <stdatomic.h>

#include "ase_common.h"
#include "ase_pcie_ats.h"

static pthread_mutex_t ase_itag_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile uint32_t itag_busy_vec;
static struct {
    uint64_t set_cycle;
    int8_t cc_expected;
    int8_t cc_received;
} itag_busy_state[32];
static uint64_t err_check_cycle;


/*
 * Encode a 64 bit physical address, translated from a VA, in a PCIe ATS
 * completion payload.
 */
uint64_t ase_pcie_ats_pa_enc(uint64_t pa, uint64_t page_len, uint32_t flags)
{
	// Valid?
	if (!pa)
		return 0;

	// Ensure the low 12 flag bits are clear
	pa &= ~INT64_C(0xfff);
	// Set the low flag bits
	pa |= flags;

	// Page size is encoded starting in bit 11 (the S flag) and then a mask of
	// ones to fill a page. See 10.2.3.2 in the PCIe standard.
	uint64_t page_mask = (page_len >> 12) - 1;
	pa |= (page_mask << 11);

	// Swizzle bytes into the required order
	return bswap_64(pa);
}


/*
 * Allocate a tag for an ATS invalidation request.
 */
int ase_pcie_ats_itag_alloc(void)
{
	// Loop until an itag is available. Needing to wait for a free tag should be
	// rare. Tags are freed by a separate thread that receives messages from the
	// RTL simulation.
	while (true) {
		if (itag_busy_vec != -1) {
			if (pthread_mutex_lock(&ase_itag_lock)) {
				ASE_ERR("pthread_mutex_lock could not attain the lock!\n");
				return -1;
			}

			int free_itag = __builtin_ffs(~itag_busy_vec);

			// ffs returns bit index + 1, so no tags are available if it's 0.
			if (free_itag) {
				free_itag -= 1;
				itag_busy_state[free_itag].cc_expected = -1;
				itag_busy_state[free_itag].cc_received = -1;
				itag_busy_state[free_itag].set_cycle = err_check_cycle;

				// Ensure set_cycle is updated before the busy vector is set
				__sync_synchronize();
				itag_busy_vec |= (1 << free_itag);

				pthread_mutex_unlock(&ase_itag_lock);
				return free_itag;
			}

			pthread_mutex_unlock(&ase_itag_lock);
		}
	}
}


/*
 * Free one or more ATS invalidation tags. The argument is the vector of
 * tag bits from the response header. Returns < 0 on error.
 */
int ase_pcie_ats_itag_free(uint32_t tag_vec, uint32_t cc)
{
	if (pthread_mutex_lock(&ase_itag_lock)) {
		ASE_ERR("pthread_mutex_lock could not attain the lock!\n");
		return -1;
	}

	// cc of 0 means 8
	if (cc == 0)
		cc = 8;

	// Loop through all 32 possible tags
	for (int t = 0; t < 32; t += 1) {
		if (tag_vec & 1) {
			if (0 == (itag_busy_vec & (1 << t))) {
				ASE_ERR("ase_pcie_ats_itag_free: released itag %d is not busy!", t);
				goto out_err;
			}

			// First response? Learn the cc value.
			if (itag_busy_state[t].cc_expected == -1) {
				itag_busy_state[t].cc_expected = cc;
				itag_busy_state[t].cc_received = 1;
			} else {
				itag_busy_state[t].cc_received += 1;

				if (itag_busy_state[t].cc_expected != cc) {
					ASE_ERR("ase_pcie_ats_itag_free: cc must be the same on all responses (%d, expected %d)\n",
						cc, itag_busy_state[t].cc_expected);
					goto out_err;
				}
			}

			if (itag_busy_state[t].cc_received == cc) {
				// Release the tag
				itag_busy_vec ^= (1 << t);
			}
		}

		// Next tag, early exit if there are no more set in the vector
		tag_vec >>= 1;
		if (!tag_vec)
			break;
	}

	pthread_mutex_unlock(&ase_itag_lock);
	return 0;

out_err:
	pthread_mutex_unlock(&ase_itag_lock);
	return -1;
}


/*
 * Check whether an ATS invalidation has been outstanding for too long.
 * Returns -1 on error.
 */
int ase_pcie_ats_itag_cycle(void)
{
	int t = 0;
	uint32_t busy_vec = itag_busy_vec;

	atomic_fetch_add(&err_check_cycle, 1);

	// The itag_busy_vec and itag_busy_state[].set_cycle are managed such
	// that locking isn't required here.
	while (busy_vec) {
		if (busy_vec & 1) {
			if (err_check_cycle - itag_busy_state[t].set_cycle > 10000) {
				ASE_ERR("PCIe ATS invalidation request has no completion (itag %d)\n", t);
				return -1;
			}
		}

		t += 1;
		busy_vec >>= 1;
	}

	return 0;
}
