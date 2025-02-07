// codice per manipolare la tlb (incluso replacement)
#include "vm_tlb.h"
#include "vm.h"
#include "vmstats.h"
#include "mips/tlb.h"
#include "addrspace.h"
#include "spl.h"
#include "pt.h"
#include "opt-dumbvm.h"
#include "opt-test.h"

/*
* usata per gestire il tlb miss
*/
int vm_fault(int faulttype, vaddr_t faultaddress){

    #if OPT_DEBUG
    print_tlb();
    #endif

    DEBUG(DB_VM,"\nindirizzo di errore: 0x%x\n",faultaddress);
    int spl = splhigh(); // in modo che il controllo non passi ad un altro processo in attesa.
    paddr_t paddr;
  
    faultaddress &= PAGE_FRAME; // Estraggo l'indirizzo del frame che ha causato l'errore (non era presente nella TLB)

    /* Aggiorno le statistiche */
    add_tlb_fault(FAULT);
    /* Estraggo l'indirizzo virtuale della pagina corrispondente */
    switch (faulttype)
    {
    case VM_FAULT_READ:
        
        break;
    case VM_FAULT_WRITE:
      
        break;
        /* Il caso di sola lettura deve essere considerato speciale: il segmento di testo non può essere scritto dal processo.
        Pertanto, se il processo cerca di modificare un segmento RO, il processo deve essere terminato tramite l'apposita chiamata di sistema (non c'è bisogno di panico) */
    case VM_FAULT_READONLY:
        kprintf("Hai cercato di scrivere su un segmento di sola lettura... Il processo sta terminando...");
        sys__exit(0);
        break;
    
    default:
        break;
    }

    /* Se sono qui, è sia un VM_FAULT_READ che un VM_FAULT_WRITE */
    /* Lo spazio degli indirizzi è stato configurato correttamente? */
    KASSERT(as_is_ok() == 1);
   /* Se lo spazio degli indirizzi è stato configurato correttamente, chiedo alla tabella delle pagine l'indirizzo virtuale del frame che non è presente nella TLB */
    paddr = get_page(faultaddress);
    /* Ora che ho l'indirizzo, posso inserirlo nella TLB */
    tlb_insert(faultaddress, paddr);
    splx(spl);
    return 0;
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
    as = proc_getas(); // I get the address space of the process
    int is_ro = 0;
    uint32_t first_text_vaddr = as->as_vbase1;
    int size = as->as_npages1;
    uint32_t last_text_vaddr = (size*PAGE_SIZE) + first_text_vaddr;
    /*if the virtual address is in the range of virtual addresses assigned to the text segment, I set is_ro to true*/
    if((vaddr>=first_text_vaddr)  && (vaddr <= last_text_vaddr))
        is_ro = 1;
    return is_ro;

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
               if(!readonly){
                    /*Devo impostare un bit di dirty che essenzialmente è un privilegio di scrittura*/
                    lo = lo | TLBLO_DIRTY; 
                }
                tlb_write(hi, lo, entry);
            /*aggiotno tlb fault free*/
            add_tlb_fault(FREE_FAULT); //TO DO: implementare add_tlb_type_fault, capire esattamente quale fault
            
            return 0;
        }

    }
    /*step 2: Non ho trovato una entry invalida, quindi,,, cerco una vittima, sovrascrivo, aggiorno la statistica corrispondente */
    entry = tlb_victim();
    hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID;
    
    if(!readonly){
        /*Devo impostare un bit di dirty che essenzialmente è un privilegio di scrittura*/
        lo = lo | TLBLO_DIRTY; 
    }
    
    tlb_read(&prevHi, &prevLo, entry);
    update_tlb_bit(prevHi, curproc->p_pid); //notifico alla pt
    tlb_write(hi, lo, entry);
    /*update tlb faults replace*/
    add_tlb_fault(REPLACE_FAULT); 
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
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    return 0;
}

/**
 * Questa funzione deve invalidare la TLB quando si passa da un processo all'altro. Infatti,
 * la TLB è comune a tutti i processi e non ha un campo "pid".
 */
void tlb_invalidate_all(void){
    uint32_t hi, lo;
    pid_t pid = curproc->p_pid; // Estraggo il pid del processo attualmente in esecuzione
    if(previous_pid != pid) // il processo (non il thread) è cambiato. Questo è necessario perché as_activate viene chiamato anche quando il thread cambia.
    {
    DEBUG(DB_VM,"NUOVO PROCESSO IN ESECUZIONE: %d AL POSTO DI %d\n",pid,previous_pid);

    /* Aggiorno le statistiche corrette */
    add_tlb_invalidation();

    /* Itero su tutte le voci */
    for(int i = 0; i<NUM_TLB; i++){
            if(tlb_entry_is_valid(i)){ // Se la voce è valida
                tlb_read(&hi,&lo,i); // recupero il contenuto
                update_tlb_bit(hi,previous_pid); // Informo tlb che la voce identificata dalla coppia (vaddr, pid) non sarà più "cached"
            }
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i); // Sovrascrivo la voce
            }
    previous_pid = pid; // Aggiorno la variabile globale previous_pid in modo che la prossima volta che la funzione viene chiamata posso determinare se il processo è cambiato.
    }
}

void print_tlb(void){
    uint32_t hi, lo;

    kprintf("\n\n\tTLB\n\n");

    for(int i = 0; i<NUM_TLB; i++){
        tlb_read(&hi, &lo, i);
        kprintf("%d virtual: 0x%x, physical: 0x%x\n", i, hi, lo);
    }

}
