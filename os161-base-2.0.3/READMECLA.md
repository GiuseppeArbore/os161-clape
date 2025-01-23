### TLB Management 
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

❗ The utility functions `tlb_read`, `tlb_writes` and the masks such as ***TLBLO_VALID*** and ***TLBLO_DIRTY*** are defined in tlb.h

### SEGMENTS
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

### SWAPFILE
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