#include "swapfile.h"



#define MAX_SIZE 9*1024*1024 // 9MB 

#if !OPT_SW_LIST
static int occ = 0;
#endif

struct swapfile *swap;



int load_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){
    int result;
    struct iovec iov;
    struct uio ku;

    KASSERT(pid==curproc->p_pid);

    #if OPT_SW_LIST

        //list: è un puntatore alla swap_cell corrente che stiamo esaminando nella lista.
        //prev: è un puntatore alla swap_cell precedente rispetto a quella attualmente esaminata (list).

        struct addrspace *as=proc_getas();
        struct swap_cell *list=NULL, *prev=NULL;
        int seg=-1; //Utilizzato sia per il debug che per la rimozione dalla testa

        //Primo passo: utilizza gli indirizzi virtuali di base (as_vbase1, as_vbase2, USERSTACK) e la dimensione in pagine (as_npages1, as_npages2) 
        //per determinare in quale segmento si trova l'indirizzo virtuale (vaddr). Capisce a quale lista di swap_cell si riferisce l'indirizzo virtuale.

        if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){
            list = swap->text[pid];
            seg=0;
        }

        if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){
            list = swap->data[pid];
            seg=1;
        }

        if(vaddr <= USERSTACK && vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE){
            list = swap->stack[pid];
            seg=2;
        }

        if(seg==-1){
            panic("Indirizzo virtuale errato per il caricamento: 0x%x, processo=%d\n",vaddr,curproc->p_pid);
        }

        //Secondo passo: cercare l'entry nella lista
        
        while(list!=NULL){

            if(list->vaddr==vaddr){ //Entry trovata
                /**
                 * Si noti che, a causa del parallelismo, è necessario imporre un ordine specifico nelle operazioni.
                 * Prima di tutto, dobbiamo rimuovere l'entry dalla lista del processo (altrimenti potremmo pensare che questa vecchia entry sia ancora valida).
                 * Quindi, possiamo eseguire l'operazione di I/O. Tuttavia, l'entry non può essere utilizzata da nessun altro (stiamo ancora lavorando su di essa), quindi non possiamo inserirla nella lista libera.
                 * Infine, dopo l'operazione di I/O possiamo inserire l'entry nella lista libera.
                */

                if(prev!=NULL){ //La lista non è la prima entry quindi rimuoviamo list saltandolo
                    KASSERT(prev->next==list);
                    DEBUG(DB_VM,"Abbiamo rimosso 0x%x dal processo %d, quindi ora 0x%x punta a 0x%x\n",vaddr,pid,prev!=NULL?prev->vaddr:(unsigned int)NULL,prev->next->vaddr);
                    prev->next=list->next; //Rimuoviamo list dalla lista del processo
                }
                else{ //Rimuoviamo dalla testa
                    if(seg==0){
                        swap->text[pid]=list->next;
                    }
                    if(seg==1){
                        swap->data[pid]=list->next;
                    }
                    if(seg==2){
                        swap->stack[pid]=list->next;
                    }
                }

                lock_acquire(list->cell_lock);
                while(list->store){ //L'entry è attualmente in fase di memorizzazione, quindi attendiamo fino a quando la memorizzazione non è stata completata
                    cv_wait(list->cell_cv,list->cell_lock); //Attendiamo sulla cv dell'entry
                }
                lock_release(list->cell_lock);
                
                DEBUG(DB_VM,"CARICAMENTO SWAP in 0x%x (virtuale: 0x%x) per il processo %d\n",list->offset, vaddr, pid);

                add_page_fault(DISK_FAULT)//Aggiorno le statistiche

                uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,list->offset,UIO_READ);//Di nuovo usiamo paddr come se fosse un indirizzo fisico del kernel per evitare una ricorsione di fault

                result = VOP_READ(swap->v,&ku);//Eseguiamo la lettura
                if(result){
                    panic("VOP_READ nel file di swap non riuscita, con risultato=%d",result);
                }
                DEBUG(DB_VM,"CARICAMENTO SWAP terminato in 0x%x (virtuale: 0x%x) per il processo %d\n",list->offset, list->vaddr, pid);

                list->next=swap->free; //Inseriamo l'entry nella testa della lista libera
                swap->free=list;

                add_page_fault(SWAPFILE_FAULT);

                list->vaddr=0; //Reimpostiamo l'indirizzo virtuale

                return 1;//Abbiamo trovato l'entry nel file di swap, quindi restituiamo 1
            }
            prev=list; //Aggiorniamo prev e list
            list=list->next;
            KASSERT(prev->next==list);
        }


    #else     
    
    int i;
    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==pid && swap->elements[i].vaddr==vaddr){// Trovo una corrispondenza
            add_page_fault(DISK_FAULT)//Aggiorno le statistiche

            swap->elements[i].pid=-1;//Dato che muovo la pagina in RAM, la marco come libera

            DEBUG(DB_VM,"SWAP: Process %d loading into RAM %lu bytes to 0x%lx (offset in swapfile : 0x%lx)\n",curproc->p_pid,(unsigned long) PAGE_SIZE, (unsigned long) paddr, (unsigned long) i*PAGE_SIZE);
            
            /*La funzione uio_kinit è utilizzata per inizializzare una struttura di tipo uio, che descrive un'operazione di input/output (I/O) in kernel space. 
            * iov: un puntatore a una struttura iovec, che descrive un singolo buffer di memoria che sarà coinvolto nell'operazione di I/O.
            *ku: un puntatore a una struttura uio, che descrive l'operazione di I/O complessiva, contenente uno o più buffer di memoria.
            *buf: un puntatore al buffer di memoria in cui i dati saranno letti o da cui saranno scritti.
            *len: la lunghezza del buffer (in byte).
            *offset: l'offset nel file da cui iniziare a leggere o scrivere.
            *rw: il tipo di operazione (lettura o scrittura), rappresentato dai valori UIO_READ o UIO_WRITE
            */
            uio_kinit(&iov, &ku, (void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE, i*PAGE_SIZE, UIO_READ); 

            result = VOP_READ(swap->v,&ku); //Questo è il momento in cui la pagina viene effettivamente caricata dal file di swap in RAM.
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            add_page_fault(SWAPFILE_FAULT)

            occ--;

            DEBUG(DB_VM,"Process %d read. Now occ=%d\n",curproc->p_pid,occ);

            return 1;//troviamo la entry nel swapfile, quindi ritorniamo 1
        }
    }
    #endif

    return 0;//Non trovi la entry nel swapfile, quindi ritorniamo 0
}

int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    int result;
    struct iovec iov;
    struct uio ku;

    #if OPT_SW_LIST

    struct addrspace *as=proc_getas();
    struct swap_cell *free_frame;
    int flag=0; //Utilizzato per capire se abbiamo provato a memorizzare un indirizzo non valido

    /**
     * Ancora una volta, a causa del parallelismo, dobbiamo prestare attenzione all'ordine delle operazioni.
     * All'inizio, prendiamo un frame libero dalla lista dei liberi. Tuttavia, non possiamo inserirlo nella lista dei processi dopo l'I/O.
     * Infatti, se, mentre l'operazione di memorizzazione è in corso, il processo cerca questa entry nello swap, non la troverà.
     * A causa di ciò, la caricherà dall'ELF, ma ovviamente questo è sbagliato e porterà a problemi.
     * Tuttavia, durante l'operazione di memorizzazione non possiamo accedere alla pagina, perché non conterrà dati validi. Per risolvere questo problema
     * introduciamo il campo store, che mostrerà se c'è un'operazione di memorizzazione in corso per quel frame.
    */

    free_frame=swap->free; //Prendiamo un frame libero dalla lista dei liberi

    if(free_frame==NULL){
        panic("Il file di swap è pieno!"); //Se non abbiamo trovato nessuna entry libera, il file di swap è pieno e facciamo panic
    }

    KASSERT(free_frame->store==0);

    swap->free = free_frame->next; //Aggiorniamo la lista dei liberi

    //Identifichiamo il segmento dell'indirizzo virtuale e facciamo un inserimento in testa

    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){
        free_frame->next=swap->text[pid];
        swap->text[pid]=free_frame;
        flag=1;
    }

    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){
        free_frame->next=swap->data[pid];
        swap->data[pid]=free_frame;
        flag=1;
    }

    if(vaddr <= USERSTACK && vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE){
        free_frame->next=swap->stack[pid];
        swap->stack[pid]=free_frame;
        flag=1;
    }


    if(flag==0){
        panic("Indirizzo virtuale errato per la memorizzazione: 0x%x\n",vaddr);
    }

    free_frame->vaddr=vaddr; //Dobbiamo impostare l'indirizzo corretto qui e non dopo la memorizzazione



    DEBUG(DB_VM,"MEMORIZZAZIONE SWAP in 0x%x (virtuale: 0x%x) per il processo %d\n",free_frame->offset, free_frame->vaddr, pid);

    free_frame->store=1; //Impostiamo il flag di memorizzazione a 1

    uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,free_frame->offset,UIO_WRITE);

    result = VOP_WRITE(swap->v,&ku);//Scriviamo sul file di swap
    if(result){
        panic("VOP_WRITE nel file di swap non riuscita, con risultato=%d",result);
    }

    free_frame->store=0; //Cancelliamo il flag di memorizzazione

    lock_acquire(free_frame->cell_lock);
    cv_broadcast(free_frame->cell_cv, free_frame->cell_lock); //Svegliamo i processi che erano in attesa che la memorizzazione fosse completata
    lock_release(free_frame->cell_lock);

    DEBUG(DB_VM,"MEMORIZZAZIONE SWAP TERMINATA in 0x%x (virtuale: 0x%x) per il processo %d\n",free_frame->offset, free_frame->vaddr, pid);

    DEBUG(DB_VM,"Abbiamo aggiunto 0x%x al processo %d, che punta a 0x%x\n",vaddr,pid,free_frame->next?free_frame->next->vaddr:0x0);

    add_swapfile_write();//Aggiorniamo le statistiche

    return 1;

    #else

    int i;
    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==-1){//ricerca per una entry libera

            DEBUG(DB_VM,"SWAP: Loading from RAM %lu bytes from 0x%lx (offset in swapfile : 0x%lx)\n",(unsigned long) PAGE_SIZE, (unsigned long) paddr, (unsigned long) i*PAGE_SIZE);

            swap->elements[i].pid=pid;
            swap->elements[i].vaddr=vaddr;//Assegniamo la pagina al processo
            
            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,i*PAGE_SIZE,UIO_WRITE);

            result = VOP_WRITE(swap->v,&ku);// scrivo lo swapfile
            if(result){
                panic("VOP_WRITE in swapfile failed, with result=%d",result);
            }

            add_swapfile_write();//Aggiorniamo le statistiche

            occ++;

            DEBUG(DB_VM,"Process %d wrote. Now occ=%d\n",curproc->p_pid,occ);
            
            return 1;
        }
    }

    panic("The swapfile is full!");

    #endif

}

int swap_init(void){
    int result;
    int i;
    char fname[9];
    #if OPT_SW_LIST
    struct swap_cell *tmp;
    #endif

    strcpy(fname,"lhd0raw:");//Questo nome rappresenta un dispositivo raw di un disco (ad esempio, un disco fisico virtuale) che verrà utilizzato come file di swap.

    swap = kmalloc(sizeof(struct swapfile));
    if(!swap){
        panic("Error during swap allocation");
    }

    /**
     * vfs_open: apre il file di swap (che è in realtà un dispositivo raw rappresentato da lhd0raw:) con i permessi di lettura e scrittura (O_RDWR).

    fname: nome del file da aprire.
    O_RDWR: flag per indicare apertura in lettura e scrittura.
    0: nessun flag aggiuntivo.
    &swap->v: puntatore alla variabile dove memorizzare il vnode (rappresentazione del file nel VFS)
    */

    result = vfs_open(fname, O_RDWR , 0, &swap->v);//We open the swapfile
    if (result) {
		return result;
	}

    swap->size = MAX_SIZE/PAGE_SIZE;//Numero di pagine nel nostro swapfile

    #if OPT_SW_LIST

    swap->kbuf = kmalloc(PAGE_SIZE); //Invece di allocare e liberare ogni volta kbuf per eseguire la copia di swap, lo allociamo una volta sola
    if(!swap->kbuf){
        panic("Errore durante l'allocazione di kbuf");
    }

    swap->text = kmalloc(MAX_PROC*sizeof(struct swap_cell *)); //Una voce per ogni processo, in modo che ogni processo possa avere la sua lista
    if(!swap->text){
        panic("Errore durante l'allocazione degli elementi di testo");
    }

    swap->data = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->data){
        panic("Error during data elements allocation");
    }

    swap->stack = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->stack){
        panic("Error during stack elements allocation");
    }

    #else

    swap->elements = kmalloc(swap->size*sizeof(struct swap_cell));

    if(!swap->elements){
        panic("Error during swap elements allocation");
    }
    #endif

    #if OPT_SW_LIST
        for(i=0;i<MAX_PROC;i++){ // Inizializza tutte le liste per ogni processo
            swap->text[i]=NULL;
            swap->data[i]=NULL;
            swap->stack[i]=NULL;
        }
    #endif

    for(i=(int)(swap->size-1); i>=0; i--){//Creiamo tutti gli elementi nella lista dei liberi. Iteriamo in ordine inverso perché eseguiamo l'inserimento in testa, e in questo modo i primi elementi liberi avranno offset più piccoli.
        #if OPT_SW_LIST
        tmp=kmalloc(sizeof(struct swap_cell));
        if(!tmp){
            panic("Errore durante l'allocazione degli elementi di swap");
        }
        tmp->offset=i*PAGE_SIZE; //Offset all'interno del file di swap
        tmp->store=0;
        tmp->cell_cv = cv_create("cell_cv");
        tmp->cell_lock = lock_create("cell_lock");
        tmp->next=swap->free; //Inserimento nella lista dei liberi
        swap->free=tmp;
        #else
        swap->elements[i].pid=-1;//Segnaliamo tutte le pagine del file di swap come libere
        #endif
    }

    return 0;
}

void remove_process_from_swap(pid_t pid){
    #if OPT_SW_LIST
    struct swap_cell *elem, *next;

    // Iteriamo sulle liste di testo, dati e stack per rimuovere tutti gli elementi appartenenti al processo terminato

    if (swap->text[pid] != NULL) {
        for (elem = swap->text[pid]; elem != NULL; elem = next) {
            lock_acquire(elem->cell_lock);
            while (elem->store) { // Se c'è un'operazione di memorizzazione in corso, aspettiamo che finisca prima di inserire la pagina nella lista libera
                cv_wait(elem->cell_cv, elem->cell_lock);
            }
            lock_release(elem->cell_lock);

            next = elem->next; // Salviamo next per inizializzare correttamente elem nella successiva iterazione
            elem->next = swap->free;
            swap->free = elem;
        }
        swap->text[pid] = NULL;
    }

    if (swap->data[pid] != NULL) {
        for (elem = swap->data[pid]; elem != NULL; elem = next) {
            lock_acquire(elem->cell_lock);
            while (elem->store) {
                cv_wait(elem->cell_cv, elem->cell_lock);
            }
            lock_release(elem->cell_lock);

            next = elem->next;
            elem->next = swap->free;
            swap->free = elem;
        }
        swap->data[pid] = NULL;
    }

    if (swap->stack[pid] != NULL) {
        for (elem = swap->stack[pid]; elem != NULL; elem = next) {
            lock_acquire(elem->cell_lock);
            while (elem->store) {
                cv_wait(elem->cell_cv, elem->cell_lock);
            }
            lock_release(elem->cell_lock);

            next = elem->next;
            elem->next = swap->free;
            swap->free = elem;
        }
        swap->stack[pid] = NULL;
    }

    #else
    int i;
    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==pid){
            occ--;
            DEBUG(DB_VM,"Process %d released. Now occ=%d\n",curproc->p_pid,occ);
            swap->elements[i].pid=-1;
        }
    }
    #endif
}