/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include "spl.h"
#include "vm_tlb.h"

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret, pid_t old_pid, pid_t new_pid, int spl)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */
	newas->as_vbase1 = old->as_vbase1;
	newas->as_npages1 = old->as_npages1;
	newas->as_vbase2 = old->as_vbase2;
	newas->as_npages2 = old->as_npages2;
	newas->ph1 = old->ph1;
	newas->ph2 = old->ph2;
	newas->v = old->v;
	old->v->vn_refcount++;
	newas->initial_offset1 = old->initial_offset1;
	newas->initial_offset2 = old->initial_offset2;

	prepare_copy_pt(old_pid);
	prepare_copy_swap(old_pid, new_pid);
	copy_swap_pages(new_pid, old_pid, spl);
	copy_pt_entries(old_pid, new_pid);
	end_copy_pt(old_pid);
	end_copy_swap(new_pid);


	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	if(as->v->vn_refcount==1){
		vfs_close(as->v);
	}
	else{
		as->v->vn_refcount--;
	}

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;
	int spl;
	spl = splhigh();
	as = proc_getas();

	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
	tlb_invalidate_all();
	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	size_t npages;
	size_t initial_offset;

	/* Allineo la regione*/
	// allineo la base della regione alla pagina più vicina
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	initial_offset = vaddr % PAGE_SIZE;
	vaddr &= PAGE_FRAME;

	// verifico la lunghezza
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = memsize / PAGE_SIZE;


	//CLAPE: Non le stiamo usando, capire che farci
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->initial_offset1 = initial_offset;
		return 0;
	}
	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->initial_offset2 = initial_offset;
		return 0;
	}

	kprintf("Too many regions\n");
	return ENOSYS;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

int as_is_ok(void){
	struct addrspace *as = proc_getas();

	if (as == NULL)
	{
		return 0;
	}
	else if(as->as_vbase1 == 0 || 
		as->as_vbase2 == 0 ||
		as->as_npages1 == 0 ||
		as->as_npages2 == 0 ||
		){
		return 0;
	}
	
	return 1;
}


void vm_bootstrap(void){
	swap_init();
	pt_init();
}

void vm_tlbshootdown(const struct tlbshootdown *ts){
	(void)ts;
	//panic("tlbshootdown");
	tlb_invalidate_all();
}

void vm_shutdown(void){
	for (int i = 0; i < page_table->n_entry; i++)
	{
		if (page_table->entries[i].ctrl!=0)
		{
			kprintf("Page %d is still in the page table\n",i); //TODO: CLAPE: capire bene
		}
		if (page_table->entries[i].page==1)
		{
			kprintf("errore , capire bene cosa stampare\n"); //TODO: CLAPE
		}

		
	}
	
	
	stats_print();
}



void address_space_init(void){
	spinlock_init(&stealmem_lock); //todo: importare stealmem_lock
	pt_active=0;
}
/* todo: getppages , freekpages*/