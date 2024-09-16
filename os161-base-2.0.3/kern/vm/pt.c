// page tables e manipolazione delle entry della page table

#define GetValidityBit(x) (x & 1)
#define GetReferenceBit(x) ((x >> 1) & 1)
#define GetTlbBit(x) ((x >> 2) & 1)
#define GetIOBit(x) ((x >> 3) & 1)
#define GetSwapBit(x) ((x >> 4) & 1)

#define SetValidityBitOne(x) (x | 1)
#define SetReferenceBitOne(x) (x | 2)
#define SetTlbBitOne(x) (x | 4)
#define SetIOBitOne(x) (x | 8)
#define SetSwapBitOne(x) (x | 16)

#define SetReferenceBitZero(x) (x & ~2)
#define SetTlbBitZero(x) (x & ~4)
#define SetIOBitZero(x) (x & ~8)

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
#include <segments.h>
#include <cpu.h>


#define KMALLOC_PAGE 1

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
    nkmalloc = 0;
    num_frames = (mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE ; //numero di frame disponibili

    spinlock_release(&stealmem_lock);
    page_table.entries = kmalloc(num_frames * sizeof(struct pt_entry));


    spinlock_acquire(&stealmem_lock);
    if(page_table.entries == NULL){
        panic("Errore nell'allocazione della page table");
    }

    page_table.pt_lock = lock_create("pt_lock");
    page_table.pt_cv = cv_create("pt_cv");
    if (page_table.pt_lock == NULL || page_table.pt_cv == NULL){
        panic("Errore nella creazione del lock o cv della page table");
    }
    spinlock_release(&stealmem_lock);

    page_table.contiguous = kmalloc(num_frames * sizeof(int));
    spinlock_acquire(&stealmem_lock);
    if(page_table.contiguous == NULL){
        panic("Errore nell'allocazione del flag per le pagine contigue");
    }

    for (int i = 0; i < num_frames; i++){
        page_table.entries[i].ctrl = 0;
        page_table.contiguous[i] = -1;
        spinlock_release(&stealmem_lock);
        page_table.entries[i].entry_lock = lock_create("entry_lock");
        page_table.entries[i].entry_cv = cv_create("entry_cv");
        if(page_table.entries[i].entry_lock == NULL || page_table.entries[i].entry_cv == NULL){
            panic("Errore nella creazione del lock o cv della entry");
        }
        spinlock_acquire(&stealmem_lock);
    }
        
    page_table.first_free_paddr = ram_stealmem(0);

    DEBUG(DB_VM,"\nRam size :0x%x, first free address: 0x%x, memoria disponibile: 0x%x",mainbus_ramsize(),ram_stealmem(0),mainbus_ramsize()-ram_stealmem(0));

    page_table.n_entry = ((mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE) -1;
    pt_active = 1;
    spinlock_release(&stealmem_lock);
}

static int findspace() {    
    for(int i=0; i<page_table.n_entry; i++){
        if (GetValidityBit(page_table.entries[i].ctrl) || GetIOBit(page_table.entries[i].ctrl) || GetSwapBit(page_table.entries[i].ctrl)){
            continue;
        }else if(page_table.entries[i].page != KMALLOC_PAGE ){ 
            // non valido, non in uso io, non in swap, non in kmalloc
            return i; //ritorno l'indice della pagina libera
        }
    }
    return -1;
}

#if OPT_DEBUG
static int n=0;
#endif

int find_victim(vaddr_t vaddr, pid_t pid, int s){
    int i, start_index=lastIndex, first=0;
    int old_validity=0;
    pid_t old_pid;
    vaddr_t old_page;

    #if OPT_DEBUG
    if (n==0)
    {
        DEBUG(DB_VM, "Inizio a cercare vittime\n");
        n=1;
    }
    #endif

    for (i = lastIndex;; i = (i+1)%page_table.n_entry)
    {
        if (!GetTlbBit(page_table.entries[i].ctrl) && !GetIOBit(page_table.entries[i].ctrl) && page_table.entries[i].page != KMALLOC_PAGE){
            //se la pagina non è in tlb, non è in IO e non è in kmalloc
            if(GetReferenceBit(page_table.entries[i].ctrl)==0){
                //se il bit di riferimento è a 0 -> vittima trovata
                //faccio ulteriori controlli
                KASSERT(!GetIOBit(page_table.entries[i].ctrl));
                KASSERT(!GetSwapBit(page_table.entries[i].ctrl));
                KASSERT(!GetTlbBit(page_table.entries[i].ctrl));

                old_pid = page_table.entries[i].pid;
                old_validity = GetValidityBit(page_table.entries[i].ctrl);
                old_page = page_table.entries[i].page;

                page_table.entries[i].pid = pid;
                page_table.entries[i].page = vaddr;
                page_table.entries[i].ctrl=SetIOBitOne(page_table.entries[i].ctrl);
                page_table.entries[i].ctrl=SetValidityBitOne(page_table.entries[i].ctrl);

                if (old_validity){
                    store_swap(old_page, old_pid * PAGE_SIZE + page_table.first_free_paddr ); 
                }
                lastIndex = (i+1)%page_table.n_entry;
                return i;
            }else{
                page_table.entries[i].ctrl = SetReferenceBitZero(page_table.entries[i].ctrl);
            }
        }
        

        /* controllo per evitare cicli infiniti, se non trovo vittime, 
        * mi fermo e aspetto che qualcuno mi svegli (cambiamenti su condizioni delle pagine)
        */

        if((i+1) % page_table.n_entry == start_index){
            if(first==1){
                lock_acquire(page_table.pt_lock);
                splx(s);
                cv_wait(page_table.pt_cv, page_table.pt_lock);
                spl=splhigh();
                lock_release(page_table.pt_lock);
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
paddr_t pt_get_paddr(vaddr_t vaddr, pid_t pid){

    int i = get_index_from_hash(vaddr, pid);
    if (i == -1){
        return -1;
    }

    KASSERT(page_table.entries[i].pid == pid);
    KASSERT(page_table.entries[i].page == vaddr);
    KASSERT(page_table.entries[i].page != KMALLOC_PAGE);
    KASSERT(!GetIOBit(page_table.entries[i].ctrl));
    KASSERT(!GetTlbBit(page_table.entries[i].ctrl));

    page_table.entries[i].ctrl = SetTlbBitOne(page_table.entries[i].ctrl);

    return i * PAGE_SIZE + page_table.first_free_paddr;
}


paddr_t get_page(vaddr_t v){
    pid_t pid = proc_getpid(curproc); // pid corrente
    int res;
    paddr_t phisical;
    res = pt_get_paddr(v, pid, spl);

    if (res != -1)
    {
        phisical = (paddr_t) res;
        add_tlb_reload();
        return phisical;
    }

    DEBUG(DB_VM,"PID=%d wants to load 0x%x\n",pid,v);

    int pos = findspace(v,pid); 
    if (pos == -1) //non trovato libero, cerco vittima
    {
        pos = find_victim(v, pid);
        KASSERT(pos<page_table.n_entry);
        phisical = page_table.first_free_paddr + pos*PAGE_SIZE;
    }
    else{
        KASSERT(pos<page_table.n_entry);
        add_in_hash(v,pid,pos);
        phisical = page_table.first_free_paddr + pos*PAGE_SIZE;
        page_table.entries[pos].ctrl= SetValidityBitOne(page_table.entries[pos].ctrl);
        page_table.entries[pos].ctrl= SetIOBitOne(page_table.entries[pos].ctrl);
        page_table.entries[pos].page = v;
        page_table.entries[pos].pid = pid;
    }

    KASSERT(page_table.entries[pos].page !=KMALLOC_PAGE);
    load_page(v, pid, phisical);
    page_table.entries[pos].ctrl = SetIOBitZero(page_table.entries[pos].ctrl );
    page_table.entries[pos].ctrl = SetTlbBitOne(page_table.entries[pos].ctrl);

    return phisical;
}


void free_pages(pid_t pid){

    for(int i=0; i<page_table.n_entry; i++)
    {
        if(page_table.entries[i].pid == pid && GetValidityBit(page_table.entries[i].ctrl) && page_table.entries[i].page != KMALLOC_PAGE){
            
            //controllo se la pagina è in uso
            KASSERT(!GetIOBit(page_table.entries[i].ctrl));
            KASSERT(page_table.entries[i].page != KMALLOC_PAGE);
            KASSERT(!GetSwapBit(page_table.entries[i].ctrl));

            remove_from_hash(page_table.entries[i].page, pid);

            page_table.entries[i].ctrl = 0;
            page_table.entries[i].pid = 0;
            page_table.entries[i].page = 0;
        }
    }
}


int update_tlb_bit(vaddr_t v, pid_t p){
    int i;

    for(i=0; i<page_table.n_entry; i++){
        if(page_table.entries[i].pid == p && page_table.entries[i].page == v && GetValidityBit(page_table.entries[i].ctrl)){

            if(GetTlbBit(page_table.entries[i].ctrl) == 0){ 
                // se il bit tlb è a 0 -> errore
                kprintf("Error for process %d, vaddr 0x%x, ctrl=0x%x\n",p,v,page_table.entries[i].ctrl);
            }


            KASSERT(page_table.entries[i].page != KMALLOC_PAGE); 
            KASSERT(GetTlbBit(page_table.entries[i].ctrl)); 

            page_table.entries[i].ctrl = SetTlbBitZero(page_table.entries[i].ctrl);
            page_table.entries[i].ctrl = SetReferenceBitOne(page_table.entries[i].ctrl);
            return 1;
        }
    }
    return -1;
}

static int valid_entry(uint8_t ctrl, vaddr_t v){
    if (GetTlbBit(ctrl))
    {
        return 1;
    }
    if (GetValidityBit(ctrl) && GetReferenceBit(ctrl) )
    {
        return 1;
    }
    if(v == KMALLOC_PAGE){
        return 1;
    }
    if (GetIOBit(ctrl) || GetSwapBit(ctrl))
    {
        return 1;
    }
    return 0;
    
}


#if OPT_DEBUG
static int nfork=0;
#endif


paddr_t get_contiguous_pages(int n_pages, int spl){

    DEBUG(DB_VM,"Process %d performs kmalloc for %d pages\n", curproc->p_pid,npages);

    if (n_pages==1)
    {
        paddr_t phisical;
        int pos = findspace(KMALLOC_PAGE, curproc->p_pid); 
        if (pos == -1)
        {
            pos = find_victim(KMALLOC_PAGE, curproc->p_pid, spl);
            page_table.entries[pos].ctrl = SetIOBitZero(page_table.entries[pos].ctrl);
            lock_acquire(page_table.entries[pos].entry_lock);
            cv_broadcast(page_table.entries[pos].entry_cv, page_table.entries[pos].entry_lock);
            lock_release(page_table.entries[pos].entry_lock);
            lock_acquire(page_table.pt_lock);
            cv_broadcast(page_table.pt_cv, page_table.pt_lock);
            lock_release(page_table.pt_lock);
            KASSERT(pos<page_table.n_entry);
            phisical = page_table.first_free_paddr + pos*PAGE_SIZE;
        }
        else{
            KASSERT(pos<page_table.first_free_paddr);
            phisical = page_table.first_free_paddr + pos*PAGE_SIZE;
            page_table.entries[pos].ctrl= SetValidityBitOne(page_table.entries[pos].ctrl);
            page_table.entries[pos].page = KMALLOC_PAGE;
            page_table.entries[pos].pid = curproc->p_pid;
        }

        page_table.contiguous[pos]=1;

        return phisical;
    }

    int i, j, first_pos=-1, valid, prec=0, old_val, first_it=0;
    vaddr_t old_vaddr;
    pid_t old_pid;

    if (n_pages>page_table.n_entry){
        panic("Non ci sono abbastanza pagine nella page table");
    }

    for ( i = 0; i < page_table.n_entry; i++)
    {
        if (i!=0)
        {
            prec= valid_entry(page_table.entries[i-1].ctrl, page_table.entries[i-1].page);
        }
        if (!GetValidityBit(page_table.entries[i].ctrl) && 
            !GetTlbBit(page_table.entries[i].ctrl) &&
            !GetIOBit(page_table.entries[i].ctrl) &&
            !GetSwapBit(page_table.entries[i].ctrl) &&
            page_table.entries[i].page!=KMALLOC_PAGE && 
            (i==0 || prec)
        ){ 
            first_pos=i;    //sono la prima pagina del blocco contiguo
        }

        if (first_pos>=0 && 
            ! GetValidityBit(page_table.entries[i].ctrl) &&
            ! GetTlbBit(page_table.entries[i].ctrl) &&
            ! GetSwapBit(page_table.entries[i].ctrl) &&
            page_table.entries[i].page!=KMALLOC_PAGE && 
            ! GetIOBit(page_table.entries[i].ctrl) &&
            i-first_pos==n_pages-1;
        )
        {
            //se ho trovato tutte le pagine contigue che mi servivano
            DEBUG(DB_VM,"Kmalloc for process %d entry%d\n",curproc->p_pid,first);
            for (j=first_pos; j<=i; j++){
                KASSERT(page_table.entries[i].page!=KMALLOC_PAGE);
                KASSERT(!GetValidityBit(page_table.entries[j].ctrl));
                KASSERT(!GetTlbBit(page_table.entries[j].ctrl));
                KASSERT(!GetIOBit(page_table.entries[j].ctrl));
                KASSERT(!GetSwapBit(page_table.entries[j].ctrl));
                page_table.entries[i].ctrl = SetValidityBitOne(page_table.entries[j].ctrl);
                page_table.entries[i].page=KMALLOC_PAGE;
                page_table.entries[i].pid= curproc->p_pid;
            }
            page_table.contiguous[first_pos]=n_pages;
            return first_pos*PAGE_SIZE + page_table.first_free_paddr;
            
        }

    }


    #if OPT_DEBUG
    if(nfork==0){
        kprintf("FIRST FORK WITH REPLACE\n");
        nfork++;
    }
    #endif  

    while (1)
    {
        for (i=lastIndex; i<page_table.n_entry; i++)
        {
            if (page_table.entries[i].page!=KMALLOC_PAGE &&
                ! GetTlbBit(page_table.entries[i].ctrl) &&
                ! GetIOBit(page_table.entries[i].ctrl) &&
                ! GetSwapBit(page_table.entries[i].ctrl) 
            ){
                if (GetReferenceBit(page_table.entries[i].ctrl) 
                    && GetValidityBit(page_table.entries[i].ctrl)
                ){
                    page_table.entries[i].ctrl = SetReferenceBitZero(page_table.entries[i].ctrl);
                    continue;
                }
                if ( (  !GetReferenceBit(page_table.entries[i].ctrl) ||
                        !GetValidityBit(page_table.entries[i].ctrl) 
                        )
                        &&
                        (i==0 || valid_entry(page_table.entries[i-1].ctrl, page_table.entries[i-1].page))
                    ){
                        first_pos=i;
                    }    
                if( first_pos>=0 && 
                    ( !GetReferenceBit(page_table.entries[i].ctrl) || !GetValidityBit(page_table.entries[i].ctrl)) &&
                    i-first_pos==n_pages-1
                ) {
                    DEBUG(DB_VM,"Found a space for a kmalloc for process %d entry%d\n",curproc->p_pid,first_pos);
                    for(j=first_pos; j<=i; j++){
                        KASSERT(page_table.entries[j].page != KMALLOC_PAGE);
                        KASSERT(!GetValidityBit(page_table.entries[j].ctrl));
                        KASSERT(!GetTlbBit(page_table.entries[j].ctrl));
                        KASSERT(!GetIOBit(page_table.entries[j].ctrl));
                        KASSERT(!GetSwapBit(page_table.entries[j].ctrl));
                        
                        old_pid = page_table.entries[j].pid;
                        old_vaddr = page_table.entries[j].page;
                        old_val = GetValidityBit(page_table.entries[j].ctrl);

                        page_table.entries[j].pid = curproc->p_pid;
                        page_table.entries[j].page = KMALLOC_PAGE;
                        page_table.entries[j].ctrl = SetValidityBitOne(page_table.entries[j].ctrl);


                        if (old_val)
                        {
                            page_table.entries[j].ctrl = SetIOBitOne(page_table.entries[j].ctrl);
                            
                            store_swap(old_vaddr,old_pid,j * PAGE_SIZE + page_table.first_free_paddr);

                            page_table.entries[j].ctrl = SetIOBitZero(page_table.entries[j].ctrl);
                            lock_acquire(page_table.entries[j].entry_lock);
                            cv_broadcast(page_table.entries[j].entry_cv, page_table.entries[j].entry_lock);
                            lock_release(page_table.entries[j].entry_lock);
                            lock_acquire(page_table.pt_lock);
                            cv_broadcast(page_table.pt_cv, page_table.pt_lock);
                            lock_release(page_table.pt_lock);

                        }
                        
                    }
                    page_table.contiguous[first_pos]=n_pages;
                    lastIndex = (i+1)%page_table.n_entry;
                    return first_pos*PAGE_SIZE + page_table.first_free_paddr;
                }
                
                
            }

        }
        
        lastIndex=0;

        if (first_it<2)
        {
            first_it++;
        }else{
            lock_acquire(page_table.pt_lock);
            splx(spl);
            cv_wait(page_table.pt_cv, page_table.pt_lock);
            spl=splhigh();
            lock_release(page_table.pt_lock);
        }
        first_pos=-1;
        
    }
    
    return ENOMEM;
}


void free_contiguous_pages(vaddr_t vaddr){
    int i, index, niter;
    paddr_t p = KVADDR_TO_PADDR(vaddr);
    index = (p - page_table.first_free_paddr) / PAGE_SIZE;
    niter = page_table.contiguous[index];


    DEBUG(DB_VM,"Process %d performs kfree for %d pages\n", curproc?curproc->p_pid:0,niter);

    nkmalloc -= niter;
    KASSERT(niter != -1);

    for (i = index; i < index + niter; i++)
    {
        KASSERT(page_table.entries[i].page == KMALLOC_PAGE);
        page_table.entries[i].ctrl = SetValidityBitZero(page_table.entries[i].ctrl);
        page_table.entries[i].page = 0;
    }

    page_table.contiguous[index] = -1;

    DEBUG(DB_VM,"New kmalloc number after free=%d\n",nkmalloc);
}

void copy_pt_entries(pid_t old, pid_t new){
    int pos;
    for (int i = 0; i < page_table.n_entry; i++)
    {
        if ( page_table.entries[i].pid == old 
            && page_table.entries[i].page != KMALLOC_PAGE 
            && GetValidityBit(page_table.entries[i].ctrl) )
        {
            KASSERT(!GetIOBit(page_table.entries[i].ctrl));
            KASSERT(GetSwapBit(page_table.entries[i].ctrl));
            KASSERT(page_table.entries[i].page != KMALLOC_PAGE);

            page_table.entries[i].ctrl = SetIOBitOne(page_table.entries[i].ctrl);

            store_swap(page_table.entries[i].page, new , page_table.first_free_paddr + i*PAGE_SIZE);

            page_table.entries[i].ctrl = SetIOBitZero(page_table.entries[i].ctrl);

        }
    }
    #if OPT_DEBUG
    print_list(new);    //in swap_file.c
    #endif
}

void end_copy_pt(pid_t pid){
    for (int i = 0; i < page_table.n_entry; i++)
    {
        if (page_table.entries[i].pid == pid && page_table.entries[i].page != KMALLOC_PAGE && GetValidityBit(page_table.entries[i].ctrl))
        {
            KASSERT(GetSwapBit(page_table.entries[i].ctrl));
            page_table.entries[i].ctrl = SetSwapBitZero(page_table.entries[i].ctrl);
        }
    }

    lock_acquire(page_table.pt_lock);
    cv_broadcast(page_table.pt_cv, page_table.pt_lock);
    lock_release(page_table.pt_lock);
}

void prepare_copy_pt(pid_t pid){
    for (int i = 0; i < page_table.n_entry; i++)
    {
        if (page_table.entries[i].pid == pid && page_table.entries[i].page != KMALLOC_PAGE && GetValidityBit(page_table.entries[i].ctrl))
        {
            KASSERT(!GetIOBit(page_table.entries[i].ctrl));
            page_table.entries[i].ctrl = SetSwapBitOne(page_table.entries[i].ctrl);
        }
    }
}


void hashtable_init(void)  {
    htable.size= 2 *page_table.n_entry;

    htable.table = kmalloc(sizeof(struct hentry *) * htable.size);
    for (int i =0; i < htable.size; i++){
        htable.table[i] = NULL; //nessun puntatore all'interno dell'array di liste
    }

    unused_ptr_list = NULL;
    struct hashentry *tmp;
    for (int j = 0; j < page_table.n_entry; j++){ //iniziallizo unused ptr list
        tmp = kmalloc(sizeof(struct hashentry));
        KASSERT((unsigned int)tmp>0x80000000);
        if (!tmp)
        {
            panic("Error during hashpt elements allocation");
        }
        tmp->next = unusedptrlist;
        unused_ptr_list = tmp;
    } 
}

int get_index_from_hash(vaddr_t vad, pid_t pid){
    
    int val = get_hash_func(vad, pid);
    struct hashentry *tmp = htable.table[val];
    #if OPT_DEBUG
    if(tmp!=NULL)
        kprintf("Value of tmp: 0x%x, pid=%d\n",tmp->vad,tmp->pid);
    #endif
    while (tmp != NULL)
    {
        KASSERT((unsigned int)tmp>0x80000000 && (unsigned int)tmp<=0x9FFFFFFF); //verifica che il puntatore sia in keseg0 (per accesso diretto alla mem fisica)
        if (tmp->vad == vad && tmp->pid == pid)
        {
            return tmp->ipt_entry;
        }
        tmp = tmp->next;
    }
    return -1;
}

void add_in_hash(vaddr_t vad, pid_t pid, int ipt_entry){
    int val = get_hash_func(vad, pid);
    struct hashentry *tmp = unused_ptr_list;
    unused_ptr_list = unused_ptr_list->next;
    tmp->vad = vad;
    tmp->pid = pid;
    tmp->ipt_entry = ipt_entry;
    tmp->next = htable.table[val];
    htable.table[val] = tmp;
}

#if OPT_DEBUG
void print_nkmalloc(void){
    kprintf("Final number of kmalloc: %d\n",nkmalloc);
}
#endif
