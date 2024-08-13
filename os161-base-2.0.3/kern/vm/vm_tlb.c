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
    /* faultpaddr è l'indirizzo dell'inizio del frame fisico, quindi devo ricordare che non devo passare l'intero indirizzo ma devo mascherare gli ultimi 12 bit */
    int entry, valid, readonly; 
    uint32_t hi, lo, prevHi, prevLo;
    readonly = segment_is_readonly(faultvaddr); 

    /*step 1: cerca una entry libera e aggiorna la statistica corrispondente (FREE)*/
    for(entry = 0; entry < NUM_TLB; entry++){
        valid = tlb_entry_is_valid(entry);
        if(!valid){
            
                hi = faultvaddr;
                lo = faultpaddr | TLBLO_VALID;
                /*is the segment a text segment?*/
               if( readonly){
                    /*Devo impostare un bit di dirty che essenzialmente è un privilegio di scrittura*/
                    lo = lo | TLBLO_DIRTY; 
                }
                tlb_write(hi, lo, entry);
            /*aggiotno tlb fault free*/
            add_tlb_type_fault(FREE_FAULT); //TO DO: implementare add_tlb_type_fault, capire esattamente quale fault
            
            return 0;
        }

    }
    /*step 2: Non ho trovato una entry invalida, quindi,,, cerco una vittima, sovrascrivo, aggiorno la statistica corrispondente */
    entry = tlb_victim();
    hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID;
    /
    if( readonly){
        /*Devo impostare un bit di dirty che essenzialmente è un privilegio di scrittura*/
        lo = lo | TLBLO_DIRTY; 
    }
    
    tlb_read(&prevHi, &prevLo, entry);
    //TODO: notificare alla pt che quella entry non c'è piu nella tlb da capire update_tlb_bit(prevHi, curproc->p_pid);
    
    tlb_write(hi, lo, entry);
    /*update tlb faults replace*/
    add_tlb_type_fault(FREE_FAULT); //TO DO: implementare add_tlb_type_fault, capire esattamente quale fault
    return 0;

}

int tlb_entry_is_valid(int i){
    uint32_t hi, lo;
    tlb_read(&hi, &lo, i);
    /* quindi estrai il bit di validità e restituisci il risultato. Se il risultato è 0 significa che il bit di validità non è impostato e quindi l'entry è invalida. */
    return (lo & TLBLO_VALID);
}

int tlb_invalidate_entry(paddr_t paddr){
    //trovo il match nella tlb
    paddr_t frame_addr_stored;
    paddr_t frame_addr = paddr & TLBLO_PPAGE; 
    uint32_t hi, lo;
    for(int i = 0; i<NUM_TLB; i++){
        tlb_read(&hi, &lo, i);
        frame_addr_stored = lo & TLBLO_PPAGE ; // estraggo il phisical address dallo store
        if(frame_addr_stored == frame_addr)
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID());
    }
    return 0;
}

