# Progetto OS161: C1.1

## Introduzione
Il progetto ha l'obbiettivo di espandere il modulo della gestione della memoria (dumbvm), sostituendolo completamente con un gestore di memoria virtuale più avanzato, basato sulla tabella delle pagine dei progetti. 
Il progetto richiede inoltre di lavorare sulla TLB (Translation Lookaside Buffer).
Il progetto è stato svolto nella variante C1.2 che prevede l'introduzione di una __Inverted Page Table__ con una hash table per velocizzare la ricerca.

## Composizione e suddivisione del lavoro

Il lavoro è stato suddiviso tra i componenti del gruppo nel seguente modo:
- g1: Giuseppe Arbore (s329535): implementato parti di codice per gestire la struttura e l'accesso alla memoria virtuale di un processo assicurandone la separazione tra diversi processi (addrespace), per tenere traccia delle pagine fisiche disponibili e quelle in uso, permettendo una gestione efficiente della memoria fisica (coremap) e fornire la mappatura tra inidirizzi virtuali e fisici in modo tale da gestire la memoria virtuale e la paginazione (pt)
- g2: Claudia Maggiulli (s332252): _cosa hai fatto tu a grandi linee_ TODO

Per una miglior coordinazione si è usata una repository condivisa su GitHub e un file condiviso su Notion in modo tale di tener traccia dei vari progressi.

## Implementazione

### Address space (g1):
Per supportare l'on-demand paging, sono state apportate modifiche alla gestione dell'address space.

Il primo cambiamento riguarda la gestione dello stack: con l'on-demand paging, non è più necessario allocare a priori uno spazio di memoria contiguo per lo stack, poiché le pagine vengono caricate solo quando effettivamente necessarie.

Il secondo cambiamento interessa la gestione del loadelf, in cui lo spazio degli indirizzi virtuali viene definito in modo tale da caricare le pagine solo al momento del bisogno. Per implementare questo meccanismo, sono stati aggiunti all'interno della struttura dell'address space i riferimenti relativi ai segmenti di testo e di dati.

Nella struttura dell'address space sono anche presenti gli offset relativi a questi due segmenti, poiché potrebbero non essere allineati all'inizio di una pagina. Questo consente di gestire correttamente il caricamento delle pagine in memoria.

Inoltre, per la corretta terminazione di un processo, vengono rimosse le informazioni relative al processo dalla tabella delle pagine e dal file di swap. Per gestire correttamente la fork, viene utilizzata la funzione as_copy(), che copia le pagine del processo nella tabella delle pagine del nuovo processo, garantendo la coerenza tra i processi.

#TODO: Peppe, verifica il funzionamento della funzione as_copy().

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
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

```

#### as_create
Crea un nuovo spazio di indirizzi, alloca memoria per la struttura addrspace e ne inizializza i campi.

#### as_destroy
libera la memoria associata a uno spazio di indirizzi: 
- all'interno è implementato un conteggio dei riferimenti al file, nel caso in cui questo sia 1, il file viene effettivamente chiuso; in caso contrario, viene semplicemente decrementato il conteggio

#### as_copy
Copia un addrespace esistente, duplicando uno spazio di indirizzi esistente da un processo a un altro. 
- È utile per il fork di un processo, copiando le informazioni di memoria necessarie al nuovo processo.

#### as_activate
Attiva lo spazio di indirizzi corrente per il processo in esecuzione e invalida la TLB per evitare di usare traduzioni errate appartenenti ad un vecchio processo.

#### as_define_region
Definisce una nuova regione di memoria in uno spazio di indirizzi, imposta la virtual_base e la dimensione.

#### as_define_stack
Definisce lo spazio per lo stack utente in uno spazio di indirizzi, inizializzando il puntatore allo stack

#### as_prepare_load
prepara il caricamento dei segmenti di memoria nell'address space.

#### as_complete_load
completa caricamento dei segmenti di memoria.

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
Le funzioni preseneti in [pt.c](./kern/vm/pt.c)
Queste funzioni vengono definite in [pt.h](./kern/include/pt.h) e servono a inizializzare, effettuare conversioni di indirizzi

#### pt_init
inizializza la page table
- Calcola il numero di frame disponibili nella RAM.
- Alloca memoria per le entry della page table e inizializza le strutture di sincronizzazione (lock e condition variable).
- Inizializza ogni entry della page table con valori predefiniti e assegna i lock e le variabili di condizione a ciascuna entry.

#### copy_pt_entries
copiare all'interno della PT o del file di swap tutte le pagine del vecchio pid per il nuovo

#### prepare_copy_pt
setta a uno tutti i bit SWAP relativi al pid passato

#### end_copy_pt
setta a zero tutti i bit SWAP relativi al pid passato

#### get_page
Funzione per ottenere la pagina, a sua volta chiama pt_get_paddr o findspace per cercare spazio libero nella page table

#### pt_load_page
carica una nuova pagina dall'elf file. Se la page table è piena, seleziona la pagina da rimuovere usando l'algoritmo second-chance e lo salva nell swap file.

#### free_pages
Libera tutte le pagine associate ad un processo quando termina, rimuove le entry dalla hash table e resetta i bit di controllo.

#### findspace
Cerca una pagina libera nella tabella che non sia valida, in IO, in swap o riservata per KMALLOC.

#### find_victim
Implementa un meccanismo per trovare una pagina "vittima" da sostituire, controllando il bit di riferimento e altre condizioni.
- Scorre la page table e cerca pagine che non sono in uso
- La pagina vittima viene salvata nello spazio di swap se necessario, e la nuova pagina viene aggiunta alla hash table.

#### get_contiguous_pages
Cerca e alloca un blocco di pagine consecutive nella memoria fisica
- se necessario, trova vittime per creare spazio

#### free_contiguous_pages_
liberare le pagine contigue allocate nella ipt per un determinato indirizzo virtuale

#### pt_get_paddr
Converte l'indirizzo fisico nel corrispondente indirizzo virtuale

#### update_tlb_bit
avvisa che un frame (indirizzo virtuale) è stato rimosso dalla TLB.

---
#### hashtable_init

#### add_in_hash(
aggiungere un blocco alla hash table prendendolo da unused_ptr_list

#### get_index_from_hash
ottenere l'indice della hash table

#### remove_from_hash
rimuove una lista di blocchi dalla page_Table e la aggiunge alla lista di blocchi liberi

#### get_hash_func
calcola l'entry della hash table usando una funzione di hash


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
- at: 
- at2:
- bt:
- km1:
- km2: 


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
Prima di lanciare i test, è richiesto di aumentare la memoria RAM disponibile a 2MB (per farlo, vedere il file sys161.conf presente in root) a causa di strutture dati aggiuntive da noi usate. 
# Verificare se funziona anche con 1MB TODO CLAPE

Per lo swapfile, è stata usare la raw partition di _LHD0.img_ . Nell'implementazione, si è deciso di usare come dimensione 9MB invece dei 5MB presenti nella versione predefinita. Per allinearsi, è quindi richiesto di lanciare il seguente comando all'interno della cartella _root_:

```
disk161 resize LHD0.img 9M
```
