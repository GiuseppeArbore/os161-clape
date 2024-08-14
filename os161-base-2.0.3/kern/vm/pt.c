// page tables e manipolazione delle entry della page table

#include "pt.h"
#include "vmstats.h"
#include <types.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>

//TODO: clape ->se vogliamo fare FIFO potrebbe essere utile l'ultimo indice


/* TODOOOOO
* Questa funzione inizializza la page table, SISTEMARE
*/
/*
* numFrames: numero totale di frame disponibili nella ram
* entryInFrames: numero di entry che possono essere inserite in un frame
* framesForPt: numero di frame che la page table occupa
*/
void pt_init(void){
    int num_frames, entry_in_frames, frames_for_pt;
    num_frames = ram_getsize() / PAGE_SIZE; //dim_ram/dim_frame
    entry_in_frames = PAGE_SIZE / sizeof(struct pt_entry) + ((PAGE_SIZE % sizeof(struct pt_entry)) ? 1 : 0); //dim_frame/dim_entry
    //aggiunta la somma per gestire il caso in cui il frame non sia perfettamente divisibile per la dimensione della entry
    //in tal caso, sommo 1 per avere spazio a sufficienza
    frames_for_pt = num_frames / entry_in_frames + ((num_frames % entry_in_frames) ? 1 : 0); 
    page_table = kmalloc(sizeof(struct pt_info)); //TODO: clape
    /*
    * STO ALLOCANDO PT_INFO MA PENSO CHE DOVREI ALLOCARE PT_ENTRY*NUM FRAMES 
    * E POI VALUTARE PT_INFO --> TO DO DOOOOOOOOOOOOOOOOOOOOO
    */
    
    if(page_table == NULL){
        panic("Errore nell'allocazione della page table");
    }
    page_table->first_free_paddr = 0;
    page_table->entries = kmalloc(num_frames * sizeof(struct pt_entry));
    page_table -> n_entry = num_frames;
    for (int i = 0; i < num_frames; i++){
        page_table->entries[i].valid = 0;
        page_table->entries[i].dirty = 0;
        page_table->entries[i].used = 0;
        page_table->entries[i].swap = 0;
        page_table->entries[i].elf = 0;
        page_table->entries[i].pid = -1;
        //TODO: clape 
    }
    pt_active = 1;
    
}

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