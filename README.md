# Progetto OS161: C1.1

## Introduzione
Il progetto ha l'obbiettivo di espandere il modulo della gestione della memoria (dumbvm), sostituendolo completamente con un gestore di memoria virtuale più avanzato, basato sulla tabella delle pagine dei progetti. 
Il progetto richiede inoltre di lavorare sulla TLB (Translation Lookaside Buffer).
Il progetto è stato svolto nella variante C1.2 che prevede l'introduzione di una __Inverted Page Table__ con una hash table per velocizzare la ricerca.

## Composizione e suddivisione del lavoro

Il lavoro è stato suddiviso tra i componenti del gruppo nel seguente modo:
- g1: Giuseppe Arbore (s329535): implementato parti di codice per gestire la struttura e l'accesso alla memoria virtuale di un processo assicurandone la separazione tra diversi processi (addrespace), per tenere traccia delle pagine fisiche disponibili e quelle in uso, permettendo una gestione efficiente della memoria fisica (coremap) e fornire la mappatura tra inidirizzi virtuali e fisici in modo tale da gestire la memoria virtuale e la paginazione (page table)
- g2: Claudia Maggiulli (s332252): implementato la gestione della TLB, garantendo un corretto funzionamento della memoria virtuale con la nuova politica on-demand,  gestito la segmentazione della memoria virtuale, distinguendo tra i diversi segmenti di un processo e garantendo un corretto allineamento con il nuovo modello di paging on-demand delle pagine dai file ELF o dallo swap file, e ho sviluppato un sistema di swap efficiente con una struttura dati ottimizzata per gestire il trasferimento delle pagine tra RAM e disco.

Per una miglior coordinazione si è usata una repository condivisa su GitHub e un file condiviso su Notion in modo tale di tener traccia dei vari progressi.

## Implementazione

### Address space (g1):
Per supportare l'on-demand paging, sono state apportate modifiche alla gestione dell'address space.

Il primo cambiamento riguarda la gestione dello stack: con l'on-demand paging, non è più necessario allocare a priori uno spazio di memoria contiguo per lo stack, poiché le pagine vengono caricate solo quando effettivamente necessarie.

Il secondo cambiamento interessa la gestione del loadelf, in cui lo spazio degli indirizzi virtuali viene definito in modo tale da caricare le pagine solo al momento del bisogno. Per implementare questo meccanismo, sono stati aggiunti all'interno della struttura dell'address space i riferimenti relativi ai segmenti di testo e di dati.

Nella struttura dell'address space sono anche presenti gli offset relativi a questi due segmenti, poiché potrebbero non essere allineati all'inizio di una pagina. Questo consente di gestire correttamente il caricamento delle pagine in memoria.

Inoltre, per la corretta terminazione di un processo, vengono rimosse le informazioni relative al processo dalla tabella delle pagine e dal file di swap. Per gestire correttamente la fork, viene utilizzata la funzione as_copy(), che copia le pagine del processo nella tabella delle pagine del nuovo processo, garantendo la coerenza tra i processi.


L'address space è diviso in due segmenti: data e stack.
#### Struttura dati
```c
struct addrspace {
        vaddr_t as_vbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        size_t as_npages2;
        Elf_Phdr ph1;//Program header of the text section
        Elf_Phdr ph2;//Program header of the data section
        struct vnode *v; //vnode of the elf  - eseguibile
        size_t initial_offset1;
        size_t initial_offset2;
        int valid;
}
```


#### Implementazione
Le funzioni presenti in [addrespace.c](./kern/vm/addrespace.c) si occupano della gestione degli spazi di indirizzi e delle operazioni di memoria virtuale per OS/161, le loro definizioni sono in [addrespace.h](./kern/include/addrspace.h).
```c
struct addrspace *as_create(void);
int               as_copy(struct addrspace *old, struct addrspace **ret, pid_t old_pid, pid_t new_pid);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);
int               as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz, int readable, int writeable, int executable);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

```

#### as_create
Crea un nuovo spazio di indirizzi, alloca memoria per la struttura addrspace e ne inizializza i campi.
```c
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;

	return as;
```

#### as_destroy
libera la memoria associata a uno spazio di indirizzi: 
- all'interno è implementato un conteggio dei riferimenti al file, nel caso in cui questo sia 1, il file viene effettivamente chiuso; in caso contrario, viene semplicemente decrementato il conteggio
```c
	if(as->v->vn_refcount==1){
		vfs_close(as->v);
	}
	else{
		as->v->vn_refcount--;
	}

	kfree(as);
```

#### as_copy
Copia un addrespace esistente, duplicando uno spazio di indirizzi esistente da un processo a un altro. 
- È utile per il fork di un processo, copiando le informazioni di memoria necessarie al nuovo processo.

```c
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

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
	copy_swap_pages(new_pid, old_pid);
	copy_pt_entries(old_pid, new_pid);
	end_copy_pt(old_pid);

	*ret = newas;
	return 0;

```

#### as_activate
Attiva lo spazio di indirizzi corrente per il processo in esecuzione e invalida la TLB per evitare di usare traduzioni errate appartenenti ad un vecchio processo.

#### as_define_region
Definisce una nuova regione di memoria in uno spazio di indirizzi, imposta la virtual_base e la dimensione.
```c
	size_t npages;
	size_t initial_offset;

	/* Allineo la regione*/
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME; // aggiungo la parte non allineata dopo aver isolato l'offset di vaddr
	initial_offset = vaddr % PAGE_SIZE;
	vaddr &= PAGE_FRAME;

	// verifico la lunghezza
	memsize = (memsize + initial_offset + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = memsize / PAGE_SIZE;

	if (as->as_vbase1 == 0) {
		DEBUG(DB_VM, "as_define_region, text : vaddr 0x%x \n", vaddr);
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->initial_offset1 = initial_offset;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		DEBUG(DB_VM, "as_define_region, data : vaddr 0x%x \n", vaddr);
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->initial_offset2 = initial_offset;
		return 0;
	}

	kprintf("dumbvm: Warning : Too many regions\n");
	return ENOSYS;
```

#### as_define_stack
Definisce lo spazio per lo stack utente in uno spazio di indirizzi, inizializzando il puntatore allo stack


### Page table (g1):
La page table utilizzata è un'Inverted Page Table, che associa un indirizzo virtuale e l'ID del processo alla corrispondente entry in RAM. Questo approccio riduce il consumo di memoria rispetto a una tradizionale page table, poiché mantiene una sola tabella globale invece di una per ogni processo. La gestione del rimpiazzo delle pagine segue un algoritmo basato sulla tecnica del Second Chance, combinata con una coda FIFO.

Quando una pagina non è presente nella TLB e deve essere sostituita, l'algoritmo controlla il bit di riferimento:
- Se il bit di riferimento è 0, la pagina viene considerata una vittima e può essere rimossa.
- Se il bit di riferimento è 1, viene azzerato e la ricerca continua in modo circolare, dando così alla pagina una "seconda possibilità" prima di essere sostituita.
Per garantire correttezza ed evitare errori, alcune categorie di pagine vengono escluse dalla sostituzione:

- Pagine interessate da operazioni di I/O:
Queste non possono essere rimosse perché un'operazione di I/O su una pagina assente fallirebbe. La sostituzione è bloccata fino al completamento dell'operazione.

- Pagine coinvolte in una fork:
Durante un'operazione di fork, le pagine potrebbero essere in uno stato transitorio e non completamente copiate. Per evitare incoerenze (ad esempio, pagine non copiate o stati incoerenti), tali pagine vengono protette fino al termine della fork. Questo controllo viene effettuato verificando il swap bit o altri flag specifici.

- Pagine allocate con kmalloc:
Le pagine allocate tramite kmalloc appartengono allo spazio di indirizzamento del kernel e non possono essere spostate. Ciò è dovuto al fatto che il kernel non utilizza la page table per tradurre gli indirizzi virtuali in fisici, quindi il loro spostamento potrebbe compromettere la coerenza del sistema.

Quando una pagina viene rimossa dalla TLB, il suo bit di riferimento viene settato a 1. Questo serve a evitare che la pagina venga immediatamente selezionata come vittima, poiché è probabile che venga acceduta nuovamente nel breve periodo. Tale comportamento sfrutta il principio di località temporale, considerando che le pagine presenti nella TLB sono generalmente quelle accedute più di recente.

La page table è strutturata nel seguente modo:
#### Struttura dati
```c
struct pt_info{
    struct pt_entry *entries; // array di pt_entry (IPT) 
    int n_entry; // numero di entry nella page table
    paddr_t first_free_paddr; // primo indirizzo fisico libero
    struct lock *pt_lock; 
    struct cv *pt_cv; 
    int *contiguous;    // array di flag per sapere se le pagine sono contigue 
} page_table; 
```
```c
struct pt_entry {
    vaddr_t page; // indirizzo virtuale
    pid_t pid; // pid del processo a cui appartiene la pagina
    uint8_t ctrl; // bit di controllo come: validity, reference,isInTlb, ---
};

```


#### Implementazione
Le funzioni sono presenti in [pt.c](./kern/vm/pt.c).
Queste funzioni vengono definite in [pt.h](./kern/include/pt.h) e servono a inizializzare, effettuare conversioni di indirizzi

#### pt_init
inizializza la page table
- Calcola il numero di frame disponibili nella RAM.
- Alloca memoria per le entry della page table e inizializza le strutture di sincronizzazione (lock e condition variable).
- Inizializza ogni entry della page table con valori predefiniti e assegna i lock e le variabili di condizione a ciascuna entry.

```c
    spinlock_acquire(&stealmem_lock);
    int num_frames; // entry_in_frames, frames_for_pt;

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
    }

    DEBUG(DB_VM,"\nRam size :0x%x, first free address: 0x%x, memoria disponibile: 0x%x",mainbus_ramsize(),ram_stealmem(0),mainbus_ramsize()-ram_stealmem(0));

    page_table.n_entry = ((mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE) -1;
    page_table.first_free_paddr = ram_stealmem(0);

    pt_active = 1;
    spinlock_release(&stealmem_lock)
```


#### copy_pt_entries
Copiare all'interno della PT o del file di swap tutte le pagine del vecchio pid per il nuovo
```c
    int pos;
    for (int i = 0; i < page_table.n_entry; i++)
    {
        if ( page_table.entries[i].pid == old 
            && page_table.entries[i].page != KMALLOC_PAGE 
            && GetValidityBit(page_table.entries[i].ctrl) )
        {
            pos = findspace();

            if(pos==-1){
                KASSERT(!GetIOBit(page_table.entries[i].ctrl));
                KASSERT(GetSwapBit(page_table.entries[i].ctrl));
                KASSERT(page_table.entries[i].page != KMALLOC_PAGE);
                DEBUG(DB_VM,"Copiato dall'indirizzo pt 0x%x per il processo %d\n", page_table.entries[i].page, new);
                store_swap(page_table.entries[i].page, new , page_table.first_free_paddr + i*PAGE_SIZE);

            } else{
                page_table.entries[pos].ctrl = SetValidityBitOne(page_table.entries[pos].ctrl);
                page_table.entries[pos].page = page_table.entries[i].page;
                page_table.entries[pos].pid = new;
                add_in_hash(page_table.entries[i].page, new, pos);

                memmove((void*)PADDR_TO_KVADDR(page_table.first_free_paddr+ pos*PAGE_SIZE),(void *)PADDR_TO_KVADDR(page_table.first_free_paddr + i*PAGE_SIZE), PAGE_SIZE);
                
                KASSERT(!GetIOBit(page_table.entries[pos].ctrl));
                KASSERT(!GetSwapBit(page_table.entries[pos].ctrl));
                KASSERT(!GetTlbBit(page_table.entries[pos].ctrl));
                KASSERT(page_table.entries[pos].page != KMALLOC_PAGE);

            }
        }
    }
```


#### prepare_copy_pt
Setta a uno tutti i bit SWAP relativi al pid passato
```c
    for (int i = 0; i < page_table.n_entry; i++)
    {
        if (page_table.entries[i].pid == pid && page_table.entries[i].page != KMALLOC_PAGE && GetValidityBit(page_table.entries[i].ctrl))
        {
            KASSERT(!GetIOBit(page_table.entries[i].ctrl));
            page_table.entries[i].ctrl = SetSwapBitOne(page_table.entries[i].ctrl);
        }
    }
```

#### end_copy_pt
setta a zero tutti i bit SWAP relativi al pid passato
```c
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
```

#### get_page
Funzione per ottenere la pagina, a sua volta chiama pt_get_paddr o findspace per cercare spazio libero nella page table
```c
    pid_t pid = proc_getpid(curproc); // pid corrente
    int res;
    paddr_t phisical;
    res = pt_get_paddr(v, pid);

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
```

#### pt_load_page
carica una nuova pagina dall'elf file. Se la page table è piena, seleziona la pagina da rimuovere usando l'algoritmo second-chance e lo salva nell swap file.

#### free_pages
Libera tutte le pagine associate ad un processo quando termina, rimuove le entry dalla hash table e resetta i bit di controllo.

#### findspace
Cerca una pagina libera nella tabella che non sia valida, in IO, in swap o riservata per KMALLOC.
```c
    for(int i=0; i<page_table.n_entry; i++){
        if (GetValidityBit(page_table.entries[i].ctrl) || GetIOBit(page_table.entries[i].ctrl) || GetSwapBit(page_table.entries[i].ctrl)){
            continue;
        }else if(page_table.entries[i].page != KMALLOC_PAGE ){ 
            // non valido, non in uso io, non in swap, non in kmalloc
            return i; //ritorno l'indice della pagina libera
        }
    }
    return -1;
```

#### find_victim
Implementa un meccanismo per trovare una pagina "vittima" da sostituire, controllando il bit di riferimento e altre condizioni.
- Scorre la page table e cerca pagine che non sono in uso
- La pagina vittima viene salvata nello spazio di swap se necessario, e la nuova pagina viene aggiunta alla hash table.

```c
    int i, start_index=lastIndex;
    int n_iter=0;
    int old_validity=0;
    pid_t old_pid;
    vaddr_t old_v;

    for (i = lastIndex;; i = (i+1)%page_table.n_entry)
    {
        if (!GetTlbBit(page_table.entries[i].ctrl) 
            && !GetIOBit(page_table.entries[i].ctrl) 
            && page_table.entries[i].page != KMALLOC_PAGE
            && !GetSwapBit(page_table.entries[i].ctrl)
        ){
            //se la pagina non è in tlb, non è in IO e non è in kmalloc
            if(GetReferenceBit(page_table.entries[i].ctrl)==0){
                //se il bit di riferimento è a 0 -> vittima trovata
                //faccio ulteriori controlli
                KASSERT(!GetIOBit(page_table.entries[i].ctrl));
                KASSERT(!GetSwapBit(page_table.entries[i].ctrl));
                KASSERT(!GetTlbBit(page_table.entries[i].ctrl));
                KASSERT(page_table.entries[i].page != KMALLOC_PAGE);

                old_pid = page_table.entries[i].pid;
                old_validity = GetValidityBit(page_table.entries[i].ctrl);
                old_v = page_table.entries[i].page;

                page_table.entries[i].pid = pid;
                page_table.entries[i].page = vaddr;
                page_table.entries[i].ctrl=SetIOBitOne(page_table.entries[i].ctrl);
                page_table.entries[i].ctrl=SetValidityBitOne(page_table.entries[i].ctrl);

                if (old_validity){
                    remove_from_hash(old_v, old_pid);
                    store_swap(old_v, old_pid, i* PAGE_SIZE + page_table.first_free_paddr ); 
                }
                add_in_hash(vaddr, pid, i);
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
            if(n_iter<2){
                n_iter++;
                continue;
            }else{
                lock_acquire(page_table.pt_lock);
                cv_wait(page_table.pt_cv, page_table.pt_lock);
                lock_release(page_table.pt_lock);
                n_iter=0;
            }
        }
    }
    panic("Non dovrebbe arrivare qui, non è riuscito a trovare vittime");
```

#### get_contiguous_pages
Cerca e alloca un blocco di pagine consecutive nella memoria fisica
- se necessario, trova vittime per creare spazio

```c
    DEBUG(DB_VM,"Process %d performs kmalloc for %d pages\n", curproc->p_pid,n_pages);
    int i, j, first_pos=-1, prec=0, old_val, first_it=0;
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
            i-first_pos==n_pages-1
        )
        {
            //se ho trovato tutte le pagine contigue che mi servivano
            DEBUG(DB_VM,"Kmalloc for process %d entry%d\n",curproc->p_pid,first_pos);
            for (j=first_pos; j<=i; j++){
                KASSERT(page_table.entries[j].page!=KMALLOC_PAGE);
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
                        KASSERT(!GetValidityBit(page_table.entries[j].ctrl) || !GetReferenceBit(page_table.entries[j].ctrl));
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
                            
                            remove_from_hash(old_vaddr, old_pid);
                            store_swap(old_vaddr,old_pid,j * PAGE_SIZE + page_table.first_free_paddr);

                            page_table.entries[j].ctrl = SetIOBitZero(page_table.entries[j].ctrl);
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
            
            cv_wait(page_table.pt_cv, page_table.pt_lock);
        
            lock_release(page_table.pt_lock);
            first_it=0;
        }
        first_pos=-1;    
    }
    return ENOMEM;
```

#### free_contiguous_pages_
liberare le pagine contigue allocate nella ipt per un determinato indirizzo virtuale
```c
    int i, index, niter;
    paddr_t p = KVADDR_TO_PADDR(addr);
    index = (p - page_table.first_free_paddr) / PAGE_SIZE;
    niter = page_table.contiguous[index];

    DEBUG(DB_VM,"Process %d performs kfree for %d pages\n", curproc?curproc->p_pid:0,niter);

    for (i = index; i < index + niter; i++)
    {
        KASSERT(page_table.entries[i].page == KMALLOC_PAGE);
        page_table.entries[i].ctrl = SetValidityBitZero(page_table.entries[i].ctrl);
        page_table.entries[i].page = 0;
    }

    page_table.contiguous[index] = -1;

    if(curthread->t_in_interrupt == false){
        lock_acquire(page_table.pt_lock);
        cv_broadcast(page_table.pt_cv,page_table.pt_lock); //Since we freed some pages, we wake up the processes waiting on the cv.
        lock_release(page_table.pt_lock);
    }
    else{
        cv_broadcast(page_table.pt_cv,page_table.pt_lock);
    }
```

#### pt_get_paddr
Converte l'indirizzo fisico nel corrispondente indirizzo virtuale usando la funzione get_index_from_hash
- moltiplica l'indice restituito per la dimensione delle pagine e aggiunge l'indirizzo fisico della prima pagina, per ottenere così l'indirizzo fisico cercato.


---
#### hashtable_init
```c
    htable.size= 2 *page_table.n_entry;

    htable.table = kmalloc(sizeof(struct hash_entry *) * htable.size);
    for (int i =0; i < htable.size; i++){
        htable.table[i] = NULL; //nessun puntatore all'interno dell'array di liste
    }

    unused_ptr_list = NULL;
    struct hash_entry *tmp;
    for (int j = 0; j < page_table.n_entry; j++){ //iniziallizo unused ptr list
        tmp = kmalloc(sizeof(struct hash_entry));
        KASSERT((unsigned int)tmp>0x80000000);
        if (!tmp)
        {
            panic("Error during hash pt elements allocation");
        }
        tmp->next = unused_ptr_list;
        unused_ptr_list = tmp;
    } 
```

#### add_in_hash(
Aggiunge un blocco alla hash table prendendolo da unused_ptr_list
```c
    int val = get_hash_func(vad, pid);
    struct hash_entry *tmp = unused_ptr_list;
    
    KASSERT(tmp!=NULL);

    unused_ptr_list = tmp->next;

    tmp->vad = vad;
    tmp->pid = pid;
    tmp->ipt_entry = ipt_entry;
    tmp->next = htable.table[val];

    htable.table[val] = tmp;
```

#### get_index_from_hash
Ottenere l'indice della hash table
```c
    int val = get_hash_func(vad, pid);
    struct hash_entry *tmp = htable.table[val];
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
```

#### remove_from_hash
rimuove una lista di blocchi dalla page_Table e la aggiunge alla lista di blocchi liberi
```c
    int val =  get_hash_func(v, pid);
    DEBUG(DB_VM, "Rimuovo da hash 0x%x per il processo %d, pos %d\n", v, pid, val);

    struct hash_entry *tmp = htable.table[val];
    struct hash_entry *prev = NULL;

    if (tmp == NULL){
        panic("Errore durante la rimozione dall'hash");
    }

    if (tmp->vad == v && tmp->pid==pid){
        tmp->vad = 0;
        tmp->pid = 0;
        htable.table[val] = tmp->next;
        tmp->next = unused_ptr_list;
        unused_ptr_list = tmp;

        return;
    }

    prev = tmp;
    tmp = tmp->next;

    while (tmp != NULL){
        if(tmp->vad == v && tmp->pid==pid){
            tmp->vad = 0;
            tmp->pid = 0;
            tmp->ipt_entry = -1;
            prev->next = tmp->next;
            tmp->next = unused_ptr_list;
            unused_ptr_list = tmp;
            return;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    
    panic("Non è stato trovato niente da rimuovere");    

```

#### get_hash_func
Calcola l'entry della hash table usando una funzione di hash
```c
    int val = (((int)v) % 24) + ((((int)p) % 8) << 8);
    val = val ^1234567891;
    val = val % htable.size;
    return val;
```


### Coremap (g1)
La coremap è una componente fondamentale per la gestione della memoria fisica all'interno del sistema di memoria virtuale. Questa struttura dati tiene traccia dei frame fisici disponibili nella memoria RAM del sistema. Per rappresentare lo stato dei frame è stato usato un array di interi bitmapFreeFrames dove ogni elemento rappresenta un frame fisico, in particolare se il valore è 0, il frame è libero; se il valore è 1, il frame è occupato.
Le funzioni preseneti in [coremap.c](./kern/vm/coremap.c)
Queste funzioni vengono definite in [coremap.h](./kern/include/coremap.h).

#### bitmap_init 
Funzione usata per inizializzare la bitmap, allocando e inizializzando a 0 l'array bitmapFreeFrames. Dopo aver allocato e inizializzato imposta ad 1 il flag per indicare che la coremap sia attiva.

```c
    nframes = mainbus_ramsize() / PAGE_SIZE ; // calcola il numero di frame fisici
    bitmapFreeFrames = kmalloc(nframes * sizeof(int)); // alloca la bitmap
```

#### destroy_bitmap
Disattiva la coremap impostando a 0 il flagger indicare che la coremap sia attiva e liberando la memoria allocata per la bitmap. 

#### bitmap_is_active
Verifica se la bitmap è attiva restituiendo il valore del flag bitmapFtrrFramesActive.

### TLB Management (g2)
In OS161, ogni voce della TLB include un numero di pagina virtuale (20 bit), un numero di pagina fisica (20 bit) e cinque campi, di cui gli usati sono:

- valid (1 bit): Indica se la voce della TLB è valida. Se non c’è una voce valida per la pagina virtuale richiesta, si verifica un’eccezione TLB miss (EX TLBL o EX TLBS).
- dirty (1 bit): Indica se la pagina è modificabile. Se disattivato, rende la pagina di sola lettura e genera un’eccezione EX MOD in caso di tentativo di scrittura.

Le voci valide nella TLB devono riguardare solo lo spazio di indirizzi del processo corrente. La funzione as_activate, chiamata dopo ogni cambio di contesto, invalida tutte le voci della TLB, garantendo l’aggiornamento corretto dello stato della memoria virtuale.

Sono state sviluppate le funzionalità necessarie per la gestione della TLB. Quando si verifica un TLB miss, il gestore delle eccezioni di OS/161 carica un'entrata appropriata nella TLB. Se nella TLB è presente spazio libero, la nuova entrata viene inserita direttamente. In caso contrario, il sistema sceglie un'entrata da eliminare per fare spazio alla nuova. 
Il codice relativo alla gestione di questa sezione è presente in:

```c
 kern/include/vm.h 
 kern/include/vm_tlb.h
 kern/vm/vm_tlb.c
 ```

#### vm_fault
Gestisce i "faults" della memoria virtuale che si verificano quando un processo accede a un indirizzo di memoria non presente nella TLB.
A seconda del tipo di fault (lettura, scrittura, o tentativo di scrittura su una pagina di sola lettura), la funzione adotta azioni specifiche:

- Nel caso di un tentativo di scrittura su una pagina di sola lettura, il processo viene terminato.
- Negli altri casi (lettura o scrittura su una pagina non presente nella TLB), la funzione calcola l'indirizzo fisico della pagina corrispondente tramite la tabella delle pagine e aggiorna la TLB inserendo la mappatura dell'indirizzo virtuale in indirizzo fisico.


```c
int vm_fault(int faulttype, vaddr_t faultaddress){

    #if OPT_DEBUG
    print_tlb();
    #endif

    DEBUG(DB_VM,"\nindirizzo di errore: 0x%x\n",faultaddress);
    int spl = splhigh(); 
    paddr_t paddr;
  
    faultaddress &= PAGE_FRAME;

    
    add_tlb_fault(-1);

    /* Estraggo l'indirizzo virtuale della pagina corrispondente */
    switch (faulttype)
    {
    case VM_FAULT_READ:
        
        break;
    case VM_FAULT_WRITE:
      
        break;
        
    case VM_FAULT_READONLY:
        kprintf("Hai cercato di scrivere su un segmento di sola lettura... Il processo sta terminando...");
        sys__exit(0);
        break;
    
    default:
        break;
    }


    KASSERT(as_is_ok() == 1);
   
    paddr = get_page(faultaddress);
    
    tlb_insert(faultaddress, paddr);
    splx(spl);
    return 0;
}
```
#### tlb_insert

gestisce l'inserimento di una nuova mappatura nella **TLB**. Prende un indirizzo virtuale (`faultvaddr`) e il corrispondente indirizzo fisico (`faultpaddr`) e cerca una posizione libera nella TLB:

1. Controlla se l'indirizzo virtuale è in un segmento di sola lettura con la funzione `segment_is_readonly`.
2. Cerca una entry libera nella TLB; se trovata, inserisce la mappatura virtuale-fisica.
3. Se non trova una entry libera, sceglie una entry da sovrascrivere (una vittima), aggiornando le statistiche di fault della TLB.
4. Se la pagina non è di sola lettura, abilita il permesso di scrittura con il bit `TLBLO_DIRTY`.


```c
int tlb_insert(vaddr_t faultvaddr, paddr_t faultpaddr){
   
    int entry, valid, readonly; 
    uint32_t hi, lo, prevHi, prevLo;
    readonly = segment_is_readonly(faultvaddr); 

  
    for(entry = 0; entry < NUM_TLB; entry++){
        valid = tlb_entry_is_valid(entry);
        if(!valid){       
                hi = faultvaddr;
                lo = faultpaddr | TLBLO_VALID;

               if(!readonly){
                    lo = lo | TLBLO_DIRTY; 
                }
                tlb_write(hi, lo, entry);

            add_tlb_type_fault(FREE_FAULT);
            return 0;
        }
    }
    entry = tlb_victim();
    hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID;
    
    if(!readonly){
        
        lo = lo | TLBLO_DIRTY; 
    }
    
    tlb_read(&prevHi, &prevLo, entry);
    update_tlb_bit(prevHi, curproc->p_pid); //notifico alla pt
    tlb_write(hi, lo, entry);
    add_tlb_fault(FREE_FAULT); 
    return 0;
}
```
#### tlb_invalidate_entry

Si occupa della ricerca della rispettiva entry nella tlb per poi invalidarla impostando i bit a 0.
```c
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
```

`tlb_invalidate_all` è stata anche creata per gestire al meglio l'invalidazione delle pagine al momento del cambio di contesto.
Dopo aver notato che as_activate viene chiamata per ogni thread switch si è aggiunto un controllo sul pid del processo corrente in questa funzione per accettarsi che sia  cambiato il processo e non solo in thread.

❗Le utility functions `tlb_read`, `tlb_writes` e le maschere come ***TLBLO_VALID*** and ***TLBLO_DIRTY*** sono definite in tlb.h

### SEGMENTS (g2)
Questa senzione affronta come gestire correttamente i page fault. Quando si verifica un page fault, è necessario caricare una pagina in un indirizzo fisico specifico. Questo caricamento può avvenire sia dal file ELF sia dal file di swap.
Il caricamento dal file ELF si verifica quando si accede a una particolare pagina per la prima volta. Al contrario, il caricamento dal file di swap avviene quando la pagina era stata precedentemente caricata dal file ELF ma è stata successivamente scambiata fuori dalla memoria.
Per questo in `load_elf` ,prima, è necessario controllare se la pagina è presente nello swapfile, al contrario bisogna caricarla direttamente dal file ELF.

Per il caricamento occorre determinare a quale segmento appartiene la pagina:

- Se si tratta di una pagina di testo o di dati, carichiamo la pagina utilizzando la funzione loadELFPage.
- Se si tratta di una pagina dello stack, semplicemente riempiamo la pagina di zeri.

Per caricare le pagine dal file ELF, si memorizzano gli header del programma relativi ai segmenti di testo e dati all'interno della struttura dati dello spazio degli indirizzi. Questo approccio consente di calcolare l'offset corretto all'interno del file in base all'indirizzo virtuale che si sta cercando di accedere. La formula utilizzata per questo calcolo è:

```c 
offset = as->ph.offset + (vaddr - as->as_vbase);
```

Dove vaddr rappresenta l'indirizzo virtuale in fase di accesso e as->as_vbase indica l'indirizzo di partenza del segmento. Così, l'espressione (\text{vaddr} - \text{as->as_vbase}) fornisce l'offset corretto all'interno del file rispetto all'header del programma appropriato.

È fondamentale tenere presente che il processo non accederà necessariamente alle pagine del file ELF in un ordine sequenziale. Possono esserci salti all'interno del segmento di testo o le variabili possono essere richiamate in un ordine diverso rispetto a quello in cui sono state dichiarate, rendendo quindi inaffidabile l'assunzione di un accesso sequenziale.

Tuttavia, anche se il file ELF non viene letto in modo ordinato, è certo che ogni pagina verrà caricata solo una volta. Se una pagina è già stata letta, sarà presente nella tabella delle pagine o nel file di swap, che verranno controllati prima di tentare nuovamente di accedere al file ELF, come descritto in precedenza. Questo metodo garantisce un uso efficiente della memoria e ottimizza le prestazioni complessive del sistema.

Nota la differenza tra `memsz`(grandezza del segmento in memoria) e `filesz` (grandezza del segmento nel file ELF) si possono riscontare diverse situazioni nel caricamento della pagina per cui è importante calcolare il numero di byte che si devnono effettivamente caricare.
In primo luogo, se l'offset iniziale è diverso da 0 e si tratta della prima pagina del segmento, si azzera la pagina di memoria per evitare dati residui. Si calcola quindi l'offset aggiuntivo basato sull'offset iniziale da tenere in considerazione al momento del caricamento.
Sucessivamente:
- se `sz> PAGE_SIZE` allora `sz=PAGE_SIZE` poichè non si può leggere più di una pagina.
- se `sz> 0`, la pagina sarà riempita di zeri (questo è il caso di memsz>filesz che accade quando una variabile viene dichiarata ma non inizzializzata)
- se `0<sz< PAGE_SIZE` prima la pagina sarà riempita di zero e dopo sarà caricato sz bytes (accade quando l'ultia pagina contiene spazio non usato dovuto a frammentazione interna)

### SWAPFILE (g2)
Per implementare una struttura swapfile ottimale è stata utilizzata una linked list per i frame liberi comune a tutti i processi e tre linked list(una per i segmenti di testo, una per i segenti di dati e una per i segmenti dello stack) con grandezza uguale a `MAX_PROC`. 
In questo modo ogni processo ha la sua propria lista per ogni segmento e una volta determinato il segmento a cui appartiene un determinato indirizzo virtuale, possiamo cercare la corrispondente entry nel file di swap analizzando un sottoinsieme ridotto di tutti i frame.
La struttura contiene anche il campo `kbuf` che è il buffer utilizzato per le operazioni di I/O tra swapfile e RAM durante la duplicazione di pagine di swap per le operazioni di fork. 

```c
struct swapfile{
     struct swap_cell **text;//Array di liste di pagine di testo nel file di swap (una per ogni pid)
     struct swap_cell **data;//Array di liste di pagine di dati nel file di swap (una per ogni pid)
     struct swap_cell **stack;//Array di liste di pagine di stack nel file di swap (una per ogni pid)
     struct swap_cell *free;//Lista di pagine libere nel file di swap
     //Ogni elemento di questi array è un puntatore alla testa di una lista concatenata di swap_cell per un particolare tipo di segmento (testo, dati, stack) per un processo specifico (pid).
     void *kbuf;//Buffer utilizzato durante la copia delle pagine di swap
     struct vnode *v;//vnode del file di swap
     int size;//Numero di pagine memorizzate nel file di swap
};
struct swap_cell{
    vaddr_t vaddr;//Indirizzo virtuale corrispondente alla pagina memorizzata
    int store;//Flag che indica se stiamo eseguendo un'operazione di salvataggio su una pagina specifica o meno
    struct swap_cell *next;
    paddr_t offset;//Offset dell'elemento di swap all'interno del file di swap
    struct cv *cell_cv;//Utilizzato per attendere la fine dell'operazione di salvataggio
    struct lock *cell_lock;//Necessario per eseguire cv_wait

};
```
Le tre operazioni principali eseguite sono l'inizializzazione, load e store. L'inizializzazione consiste nell'allocare le strutture dati necessarie e nell'aprire il file di swap. 
La store, invece, viene chiamata ogni volta che una pagina viene trasferita (swapped out) dalla tabella delle pagine al file di swap. Il caricamento, infine, viene eseguito ogni volta che si verifica un page fault ed è chiamato indirettamente dalla funzione `load_page` nei segmenti. 
Per gestire lo swapping in modo efficiente, tutte le inserzioni e le rimozioni dalle liste collegate avvengono in testa. È importante gestire con precisione l'ordine di queste operazioni per evitare problemi legati alla concorrenza.
Le operazioni di lettura e scrittura, infatti, sono molto simili a quelle implementate per il file ELF, così come la logica per gestire correttamente gli offset. La principale differenza è che, se si tenta di caricare una pagina che è attualmente in fase di memorizzazione, si attende il completamento dell'operazione di memorizzazione per evitare errori nelle operazioni di I/O. A questo scopo, per ogni `swap_cell` , è stato introdotto un flag (`store`)una condition variable e un lock.

```c
int load_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){
    ...
    lock_acquire(list->cell_lock);
                    while(list->store){ //L'entry è attualmente in fase di memorizzazione, quindi attendiamo fino a quando la memorizzazione non è stata completata
                        cv_wait(list->cell_cv,list->cell_lock); //Attendiamo sulla cv dell'entry
                    }
                    lock_release(list->cell_lock);
    ...
}
int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){
    ...
    lock_acquire(free_frame->cell_lock);
    cv_broadcast(free_frame->cell_cv, free_frame->cell_lock); //Svegliamo i processi che erano in attesa che la memorizzazione fosse completata
    lock_release(free_frame->cell_lock);
    ...
}
                
```
Per supportare la chiamata a fork, duplichiamo tutte le pagine di swap appartenenti al vecchio PID e le assegniamo al nuovo.
Inoltre è stato anche introdotto una leggera ottimizzazione. Quando un processo termina, le pagine nella lista libera avranno un ordine casuale per il campo offset, che dipende dall'esecuzione del programma. Poiché questo campo causa un sovraccarico (maggiore è l'offset, più lenta è l'operazione di I/O), abbiamo deciso di riordinare il campo offset dopo la conclusione del programma. In questo modo, non vedremo un calo delle prestazioni quando eseguiamo più programmi in sequenza.

```c
void reorder_swapfile(void){
    struct swap_cell *tmp=swap->free;

    for(int i=0; i<swap->size; i++){//We start with i=0 so that the first free frame has offset=0
        tmp->offset=i*PAGE_SIZE;
        tmp=tmp->next;
    }
}
```


### Altre modifiche:
Noi abbiamo anche modificato l'implementazione delle system calls necessarie (fork, waitpid, _exit) e alcune parti di `loadelf.c`. 

## Test
Per verificare l'effettivo funzionamento del sistema, sono stati usati i test già presenti all'interno di os161:
- palin: verifica la gestione della memoria e dei processi eseguendo operazioni su stringhe palindrome con fork().
- matmult: testa il corretto funzionamento dello spazio utente eseguendo una moltiplicazione tra matrici.
- huge: valuta il comportamento del sistema con processi che utilizzano grandi quantità di memoria.
- sort: controlla la gestione della memoria e delle system call ordinando un insieme di numeri.
- forktest: verifica la corretta creazione e gestione dei processi generati con fork()
- bigfork: stressa il sistema creando un numero elevato di processi per testare i limiti di fork()
- ctest: esegue un test rapido sulle system call di base per verificare il corretto funzionamento del sistema.

Inoltre, per verificare le funzioni base del kernel fossero già correttamente implementate, sono stati eseguiti i seguenti test:
- at, at2: gestione degli array,
- bt: gestione della bitmap,
- km1, km2: gestione kmalloc.


## Statistiche
Di seguito si riportano le statistiche registrate per ogni test:

| Nome test | TLB faults | TLB faults (free) | TLB faults (replace) | TLB invalidations | TLB reloads | Page faults (zeroed) | Page faults (disk) | Page faults (ELF) | Page faults (swapfile) | Swapfile writes |
|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
|palin|5|5|0|1|0|1|4|4|0|0|
|matmult|821|64|757|1|66|1|754|382|372|402|
|huge|3595|64|3531|1|403|1|3191|514|2677|2839|
|sort|1953|64|1889|1|1660|1|292|292|0|0|
|forktest|243|243|0|229|230|1|12|4|8|9|
|bigfork|27930|27930|0|101559|25250|1|2679|33|2646|2157|
|ctest|125246|64|125182|1|124986|1|259|259|0|0|


### Glossario statistiche: 

Le seguenti statistiche sono state collezionale:

1. **TLB Faults -** (`tlb_faults`)
    - Numero totale di miss nella TLB.
2. **TLB Faults with Free -** (`tlb_free_faults`)
    - Il numero di miss nella TLB che causano un inserimento in uno spazio vuoto della TLB. (spazio della TLB in cui si può aggiungere una nuova entry senza bisogno di rimpiazzamento)
3. **TLB Faults with Replace  -** (`tlb_replace_faults`)
    - Il numero di miss nella TLB che causa la scelta di una vittima da sovrascrivere con una nuova entry.
4. **TLB Invalidations -**  (`tlb_invalidations`)
    - Il numero di volte in xui l'intera TLB viene invalidata. L'operazione è eseguita ogni volta che avviene uno switch di processo.
5. **TLB Reloads** (`tlb_reloads`)
    - Il numero di miss nella TLB causato da pagine che sono già in memoria.
6. **Page Faults (Zeroed)** - (`age_zeroed_faults`)
    - Il numero di miss nella TLB che richiede una nuova pagina inizializzata a zero.
7. **Page Faults (Disk)**  - (`page_disk_faults`)
    - Il numero di miss nella TLB che richiede il caricamento di una pagina dal disco.
8. **Page Faults From Elf** - (`page_elf_faults`)
    - Il numero di Page Faults che richiede una pagina dal file ELF.
9. **Page Faults from Swapfile**  - (`page_swapfile_faults`)
    - Il numero di page fault che richiede ottenere una pagina dallo swap file.
10. **Swapfile Writes**  - (`swapfile_writes`)
    - Il numero di page faults che richiede scrivere una pagina nello swap file.
   
### Relazione tra statistiche

- La somma dei “*TLB faults with Free*” e “*TLB Faults with Replace*” deve essere uguale ai “*TLB Faults*”.
- La somma dei “*TLB Reloads*”, “*Page Faults (Disk)*” e “*Page Faults (Zeroed)*” deve essere uguale ai “*TLB Faults*”.
- La somma dei “*Page Faults from ELF*” e “*Page Faults from Swapfile*” deve essere uguale ai ”*Page Faults (Disk)*”.

  
## Note per effettuare i test
Per lo swapfile, è stata usare la raw partition di _LHD0.img_ . Nell'implementazione, si è deciso di usare come dimensione 9MB invece dei 5MB presenti nella versione predefinita. Per allinearsi, è quindi richiesto di lanciare il seguente comando all'interno della cartella _root_:

```
disk161 resize LHD0.img 9M
```
