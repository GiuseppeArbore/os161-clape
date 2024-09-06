#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include "swapfile.h"
#include "vm.h"
#include "proc.h"
#include "types.h"
#include "addrspace.h"
#include "elf.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>
#include "vmstats.h"


/**
 * Dato l'indirizzo virtuale vaddr, trova la pagina corrispondente e la carica nel paddr fornito.
 * 
 * @param vaddr: l'indirizzo virtuale che ha causato il page fault
 * @param pid: pid del processo che ha causato il page fault
 * @param paddr: l'indirizzo fisico in cui caricheremo la pagina
 * 
 * @return 0 se tutto va bene, altrimenti il codice di errore restituito da VOP_READ
 */
int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr);

#endif