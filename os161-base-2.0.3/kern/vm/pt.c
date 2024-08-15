// page tables e manipolazione delle entry della page table

#define GetValidityBit(x) (x & 0x1)
#define GetReferenceBit(x) ((x >> 1) & 0x1)
#define GetTlbBit(x) ((x >> 2) & 0x1)
#define GetIOBit(x) ((x >> 3) & 0x1)
#define GetSwapBit(x) ((x >> 4) & 0x1)

#define SetValidityBitOne(x) (x | 0x1)
#define SetReferenceBitOne(x) (x | 0x2)
#define SetTlbBitOne(x) (x | 0x4)
#define SetIOBitOne(x) (x | 0x8)

#define SetReferenceBitZero(x) (x & 0x5)
#define SetIOBitZero(x) (x & 0x7)

#include "pt.h"
#include "vmstats.h"
#include <types.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>
#include <spinlock.h>
#include <mainbus.h>
#include <current.h>
#include <proc.h>



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
    int num_frames; // entry_in_frames, frames_for_pt;
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
            // non valido, non in uso io, non in swap, non in kmalloc
            return i; //ritorno l'indice della pagina libera
        }
    }
    return -1;
}


int find_victim(vaddr_t vaddr, pid_t pid, int s){
    int i, start_index=lastIndex, first;=0
    int old_validity=0;
    pid_t old_pid;
    vaddr_t old_page;

    for (i = lastIndex;; i = (i+1)%page_table->n_entry)
    {
        if (!GetTlbBit(page_table->entries[i].ctrl) && !GetIOBit(page_table->entries[i].ctrl) && page_table->entries[i].page != KMALLOC_PAGE){
            //se la pagina non è in tlb, non è in IO e non è in kmalloc
            if(GetReferenceBit(page_table->entries[i].ctrl)==0){
                //se il bit di riferimento è a 0 -> vittima trovata
                //faccio ulteriori controlli
                KASSERT(!GetIOBit(page_table->entries[i].ctrl));
                KASSERT(!GetSwapBit(page_table->entries[i].ctrl));
                KASSERT(GetTlbBit(page_table->entries[i].ctrl));
                KASSERT(page_table->entries[i].page != KMALLOC_PAGE);

                old_pid = page_table->entries[i].pid;
                old_validity = GetValidityBit(page_table->entries[i].ctrl);
                old_page = page_table->entries[i].page;

                page_table->entries[i].pid = pid;
                page_table->entries[i].page = vaddr;
                page_table->entries[i].ctrl=SetIOBitOne(page_table->entries[i].ctrl);
                page_table->entries[i].ctrl=SetValidityBitOne(page_table->entries[i].ctrl);

                if (old_validity){
                    store_swap(old_page, old_pid * PAGE_SIZE + page_table->first_free_paddr ); //   CLAPE: da implementare0
                }
                lastIndex = (i+1)%page_table->n_entry;
                return i;
            }else{
                page_table->entries[i].ctrl = SetReferenceBitZero(page_table->entries[i].ctrl);
            }
        }
        

        if((i+1)%page_table->n_entry == start_index){
            if(first){
                lock_acquire(page_table->pt_lock);
                splx(s);
                cv_wait(page_table->pt_cv, page_table->pt_lock);
                spl=splhigh();
                lock_release(page_table->pt_lock);
            }else{
                first=1;
                continue;
            }


        }

        
    }
    panic("Non dovrebbe arrivare qui, non è riuscito a trovare vittime");
}

/*
* funzione per ottenere l'indirizzo fisico di un indirizzo logico
*/
paddr_t pt_get_paddr(vaddr_t vaddr, pid_t pid, int s){
    int validity, stopped;

    stopped = 1;    //ciclo di attesa attivo

    while (stopped){
        stopped=0; //ciclo di attesa disattivato
        for (int i=0; i<page_table->n_entry; i++){  //scorro la page table
            if (GetValidityBit(page_table->entries[i].ctrl)){
                //voce valida
                if (page_table->entries[i].pid==pid && page_table->entries[i].vaddr==vaddr){
                    //voce corrisponde a quella cercata
                    lock_acquire(page_table->entries[i].entry_lock); //acquisisco lock
                    while(GetIOBit(page_table->entries[i].ctrl)){ //se coinvolto in IO continuo a ciclare
                        stopped=1; //ciclo di attesa attivo
                        splx(s); //abilito interrupts
                        cv_wait(page_table->entries[i].entry_cv, page_table->entries[i].entry_lock); //attendo
                        spl=splhigh(); //disabilito interrupts
                        //CLAPE
                    }
                    lock_release(page_table->entries[i].entry_lock); //rilascio lock
                    if (vaddr!=page_table->entries[i].vaddr || pid!=page_table->entries[i].pid || GetValidityBit(page_table->entries[i].ctrl)==0){
                        //se la voce non corrisponde a quella cercata o non è valida
                        stopped=1; //ciclo di attesa attivo
                        continue;
                    }

                    // verifiche su bit di controllo
                    KASSERT(!GetIOBit(page_table->entries[i].ctrl)); 
                    KASSERT(!GetTlbBit(page_table->entries[i].ctrl));
                    KASSERT(page_table->entries[i].page=KMALLOC_PAGE);

                    page_table->entries[i].ctrl=SetTlbBitOne(page_table->entries[i].ctrl); //setto bit tlb ad 1
                    return i*PAGE_SIZE + page_table->first_free_paddr; //ritorno l'indirizzo fisico

                    
                }
                
            }
            

        }
    }

    return -1; //in caso di non trovato nell'IPT
}


//TODO: CLAPE rivedere perchè sono stanco
paddr_t get_page(vaddr_t v, int spl)
{

    pid_t pid = proc_getpid(curproc); // pid corrente TODO: importare
    int res;
    paddr_t phisical;
    res = pt_get_paddr(v, pid, spl);

    if (res != -1)
    {
        phisical = (paddr_t) res;
        add_tlb_reload();
        return phisical;
    }


    int pos = findspace(v,pid); 
    if (pos == -1) //non trovato libero, cerco vittima
    {
        pos = find_victim(v, pid, spl);
        KASSERT(pos<page_table->n_entry);
        phisical = page_table->first_free_paddr + pos*PAGE_SIZE;
    }
    else{
        KASSERT(pos<page_table->first_free_paddr);
        phisical = page_table->first_free_paddr + pos*PAGE_SIZE;
        page_table->entries[pos].ctrl= SetValidityBitOne(page_table->entries[pos].ctrl);
        page_table->entries[pos].ctrl= SetIOBitOne(page_table->entries[pos].ctrl);
        page_table->entries[pos].page = v;
        page_table->entries[pos].pid = pid;
    }

    KASSERT(page_table->entries[pos].page !=KMALLOC_PAGE);
    load_page(v, pid, phisical, spl);
    page_table->entries[pos].ctrl = SetIOBitZero(page_table->entries[pos].ctrl );
    lock_acquire(page_table->entries[pos].entry_lock);
    cv_broadcast(page_table->entries[pos].entry_cv,page_table->entries[pos].entry_lock);
    lock_release(page_table->entries[pos].entry_lock);
    lock_acquire(page_table->pt_lock);
    cv_broadcast(page_table->pt_cv,page_table->pt_lock);
    lock_release(page_table->pt_lock);
    page_table->entries[pos].ctrl = SetTlbBitOne(page_table->entries[pos].ctrl);

    return phisical;
}


//TODO: clape -> da rivedere se vanno aggiunti ulteriori controlli
void free_pages(pid_t pid){
    for(int i=0; i<page_table->n_entry; i++){
        if(page_table->entries[i].pid == pid && GetValidityBit(page_table->entries[i].ctrl) && page_table->entries[i].page != KMALLOC_PAGE){
            
            //controllo se la pagina è in uso
            KASSERT(!GetIOBit(page_table->entries[i].ctrl));
            KASSERT(page_table->entries[i].page != KMALLOC_PAGE);
            KASSERT(!GetSwapBit(page_table->entries[i].ctrl));


            page_table->entries[i].ctrl = 0;
            page_table->entries[i].pid = 0;
            page_table->entries[i].page = 0;
        }
    }
}


void print_nkmalloc(void){
    kprintf("Final number of kmalloc: %d\n",nkmalloc);
}