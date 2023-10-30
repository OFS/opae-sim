// Copyright(c) 2023, Intel Corporation
//
// Redistribution  and	use	 in source	and	 binary	 forms,	 with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of	 source code  must retain the  above copyright notice,
//	 this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//	 this list of conditions and the following disclaimer in the documentation
//	 and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation	 nor the names of its contributors
//	 may be used to	 endorse or promote	 products derived  from this  software
//	 without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.	IN NO EVENT	 SHALL THE COPYRIGHT OWNER	OR CONTRIBUTORS BE
// LIABLE  FOR	ANY	 DIRECT,  INDIRECT,	 INCIDENTAL,  SPECIAL,	EXEMPLARY,	OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,	BUT	 NOT LIMITED  TO,  PROCUREMENT	OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,	DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY	 OF LIABILITY,	WHETHER IN
// CONTRACT,  STRICT LIABILITY,	 OR TORT  (INCLUDING NEGLIGENCE	 OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,	EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/*
 * Interposer for mmap() et. al. to detect changes to the address space for
 * ASE's PCIe ATS emulation.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

static void* (*real_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static int (*real_munmap)(void *addr, size_t length);
static void* (*real_mremap)(void *start, size_t old_len, size_t len, int flags, ...);

static void *libase_handle;
static void (*dl_ase_mem_unmap_hook)(void *va, size_t length);

/*
 * Find ASE notifier hooks that will be called to tell ASE about memory updates.
 */
static void load_ase_hooks(void)
{
	static int attempts;

	// Already loaded?
	if (libase_handle)
		return;

	// Give up after a while. libase is loaded dynamically, so don't give up
	// immediately.
	if (attempts > 1000)
		return;
	attempts += 1;

	// Look for ASE, find it only if already loaded into the process
	libase_handle = dlopen("libase.so", RTLD_LAZY | RTLD_NOLOAD);
	if (!libase_handle)
		return;

	dl_ase_mem_unmap_hook = dlsym(libase_handle, "ase_mem_unmap_hook");
}


/*
 * Wrap mmap() in case addr was specified and overwrites a previous mapping.
 */
void __attribute__((visibility("default"))) *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	if (!real_mmap)
		real_mmap = dlsym(RTLD_NEXT, "mmap");

	load_ase_hooks();

	if (real_mmap)
	{
		if (addr && dl_ase_mem_unmap_hook)
			(*dl_ase_mem_unmap_hook)(addr, length);

		return (*real_mmap)(addr, length, prot, flags, fd, offset);
	}

	errno = EINVAL;
	return MAP_FAILED;
}

/*
 * Invalidate translation cache of unmapped region.
 */
int __attribute__((visibility("default")))
munmap(void *addr, size_t length)
{
	if (!real_munmap)
		real_munmap = dlsym(RTLD_NEXT, "munmap");

	load_ase_hooks();

	if (real_munmap)
	{
		if (dl_ase_mem_unmap_hook)
			(*dl_ase_mem_unmap_hook)(addr, length);

		return (*real_munmap)(addr, length);
	}

	errno = EINVAL;
	return -1;
}

/*
 * Invalidate translation cache of remapped region.
 */
void  __attribute__((visibility("default"))) *
mremap (void *start, size_t old_len, size_t len, int flags, ...)
{
	void *result = NULL;
	va_list ap;

	va_start (ap, flags);
	void *newaddr = (flags & MREMAP_FIXED) ? va_arg (ap, void *) : NULL;
	va_end (ap);

	if (!real_mremap)
		real_mremap = dlsym(RTLD_NEXT, "mremap");

	load_ase_hooks();

	if (real_mremap)
	{
		if (dl_ase_mem_unmap_hook)
			(*dl_ase_mem_unmap_hook)(start, old_len);

		if (flags & MREMAP_FIXED) {
			if (dl_ase_mem_unmap_hook)
				(*dl_ase_mem_unmap_hook)(newaddr, len);

			return (*real_mremap)(start, old_len, len, flags, newaddr);
		}
		else
			return (*real_mremap)(start, old_len, len, flags);
	}

	errno = EINVAL;
	return MAP_FAILED;
}
