// codice per manipolare la tlb (incluso replacement)
#include "vm_tlb.h"
#include "vm.h"
#include "vmstats.h"
#include "mips/tlb.h"
#include "addrspace.h"
#include "spl.h"
#include "pt.h"

/*
* usata per gestire il tlb miss
*/
int vm_fault(int faulttype, vaddr_t faultaddress){
    return -1;
}

int tlb_victim(void){
    // RR algoritmo
    int victim;
    static unsigned int next_victim = 0;
    victim = next_victim;
    next_victim = (next_victim + 1) % NUM_TLB;
    return victim;
}

int segment_is_readonly(vaddr_t vaddr){
    struct addrspace *as;
    as = proc_getas();
    int readonly = 0;
    unit32_t vbfirst_text_vaddr = as->as_vbase1;
    inti size = as-> as_npages1;
    unit32_t last_text_vaddr = (size * PAGE_SIZE) + first_text_vaddr;
    if(vaddr >= vbase && vaddr < last_text_vaddr){
        readonly = 1;
    }
    return readonly;
}

int tlb_insert(vaddr_t faultvaddr, paddr_t faultpaddr){
    
    int i, valid, readonly;
    uint32_t ehi, elo;
    readonly = segment_is_readonly(faultvaddr);

    //cerco una entry libera e aggiorno statistiche 
    for(i = 0; i < NUM_TLB; i++){
        if(!tlb_entry_is_valid(i)){
            stats_tlb_fault(FREE_FAULT);
            break;
        }
    }
      
    return 0;
}