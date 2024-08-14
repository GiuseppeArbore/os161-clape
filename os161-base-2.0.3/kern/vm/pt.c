// page tables e manipolazione delle entry della page table
#define PAGE_SIZE 4096

#define GetValidityBit(x) (x & 0x1)
#define GetReferenceBit(x) ((x >> 1) & 0x1)
#define GetTlbBit(x) ((x >> 2) & 0x1)
#define GetIOBit(x) ((x >> 3) & 0x1)
#define GetSwapBit(x) ((x >> 4) & 0x1)
#include "pt.h"
#include "vmstats.h"
#include <types.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>
#include <spinlock.h>
#include <mainbus.h>

#define KMALLOC_PAGE 1
//TODO: clape ->se vogliamo fare FIFO potrebbe essere utile l'ultimo indice
int lastIndex = 0;

/**
* Questa funzione inizializza la page table
*
* @var: numFrames: numero totale di frame disponibili nella ram
* @var: entryInFrames: numero di entry che possono essere inserite in un frame
* @var: framesForPt: numero di frame che la page table occupa
*/
void pt_init(void){
    int num_frames, entry_in_frames, frames_for_pt;
    spinlock_acquire(&stealmem_lock);
    num_frames = (mainbus_ramsize() - ram_stealmem) / PAGE_SIZE ; //numero di frame disponibili


    /* TODOOO: CLAPE, l'avevp fatta così, ma non so se effettivamente serva
    entry_in_frames = PAGE_SIZE / sizeof(struct pt_entry) + ((PAGE_SIZE % sizeof(struct pt_entry)) ? 1 : 0); //dim_frame/dim_entry
    //aggiunta la somma per gestire il caso in cui il frame non sia perfettamente divisibile per la dimensione della entry
    //in tal caso, sommo 1 per avere spazio a sufficienza
    frames_for_pt = num_frames / entry_in_frames + ((num_frames % entry_in_frames) ? 1 : 0); 
    page_table = kmalloc(sizeof(struct pt_info)); //TODO: clape
    /*
    * STO ALLOCANDO PT_INFO MA PENSO CHE DOVREI ALLOCARE PT_ENTRY*NUM FRAMES 
    * E POI VALUTARE PT_INFO --> TO DO DOOOOOOOOOOOOOOOOOOOOO
    */
    spinlock_release(&stealmem_lock);


    page_table->entries = kmalloc(num_frames * sizeof(struct pt_entry));
    spinlock_acquire(&stealmem_lock);
    if(page_table->entries == NULL){
        panic("Errore nell'allocazione della page table");
    }
    page_table->pt_lock = lock_create("pt_lock");
    page_table->pt_cv = cv_create("pt_cv");
    if (page_table->pt_lock == NULL || page_table->pt_cv == NULL){
        panic("Errore nella creazione del lock o cv della page table");
    }
    spinlock_release(&stealmem_lock);

    page_table->contiguous = kmalloc(num_frames * sizeof(int));
    spinlock_acquire(&stealmem_lock);
    if(page_table->contiguous == NULL){
        panic("Errore nell'allocazione del flag per le pagine contigue");
    }

    for (int i = 0; i < num_frames; i++){
        page_table->entries[i].ctrl = 0;
        page_table->contiguous[i] = -1;
        spinlock_release(&stealmem_lock);
        page_table->entries[i].entry_lock = lock_create("entry_lock");
        page_table->entries[i].entry_cv = cv_create("entry_cv");
        if(page_table->entries[i].entry_lock == NULL || page_table->entries[i].entry_cv == NULL){
            panic("Errore nella creazione del lock o cv della entry");
        }
        spinlock_acquire(&stealmem_lock);
    }
        
    page_table->first_free_paddr = ram_stealmem(0);
    //TODO: clape -> non so se serve
    page_table -> n_entry = num_frames -1;
    pt_active = 1;
    spinlock_release(&stealmem_lock);
}

static int findspace() {    
    for(int i=0; i<page_table->n_entry; i++){
        if (GetValidityBit(page_table->entries[i].ctrl) || GetIOBit(page_table->entries[i].ctrl) || GetSwapBit(page_table->entries[i].ctrl)){
            return -1;
        }else if(page_table->entries[i].page != KMALLOC_PAGE ){ 
            return i; //ritorno l'indice della pagina libera
        }
    }
    return -1;
}

// TODO: RIVISTO FIN QUI

/*
* funzione per ottenere l'indirizzo fisico di un indirizzo logico
*/
paddr_t pt_get_paddr(vaddr_t vaddr, pid_t pid){
    if(!pt_active){
        return 0;
    }
    for(int i=0; i<page_table->n_entry; i++){
        if(page_table->entries[i].vaddr == vaddr && page_table->entries[i].pid == pid && page_table->entries[i].valid){
            
            //TODO: clape:_ ci sono alcuni bit da settare per la gestione della page table
            //es: bit che indica pagina usata
            //es: bit che indica pagina è in tlb
            return page_table->entries[i].paddr;
        }
    }
    return NULL; //in caso di non trovato
}
/*
* funzione per caricare una nuova pagina nella memoria fisica
* e di aggiornare la Page Table e la TLB 
*/
paddr_t pt_load_page(vaddr_t vaddr, pid_t pid){
    if(!pt_active){
        return 0;
    }
    int i;
    for(i=0; i<page_table->n_entry; i++){
        if(!page_table->entries[i].valid){
            break;
        }
    }
    if(i == page_table->n_entry){
    }
 //TODO: devo finirla :: kjdfnvbkjldf
}



int free_pages(pid_t pid){
    int count = 0;
    for(int i=0; i<page_table->n_entry; i++){
        if(page_table->entries[i].pid == pid){
            page_table->entries[i].valid = 0;
            page_table->entries[i].dirty = 0;
            page_table->entries[i].used = 0;
            page_table->entries[i].swap = 0;
            page_table->entries[i].elf = 0;
            page_table->entries[i].pid = -1;
            count++;
        }
    }
    return count;
}

