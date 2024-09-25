#include "segments.h"


/**
 * Questa funzione legge una pagina dal file elf nella virtual address fornita.
 * 
 * @param v: il vnode del file elf
 * @param offset: l'offset utilizzato all'interno del file elf
 * @param vaddr: l'indirizzo virtuale in cui possiamo memorizzare i dati letti
 * @param memsize: la dimensione della memoria da leggere
 * @param filesize: la dimensione del file da leggere
 * 
 * @return 0 se tutto va bene, altrimenti il codice di errore restituito da VOP_READ
 * 
*/

/*Struttura iovec (iov)

    iov_ubase: l'indirizzo di base dell'area di memoria utente dove i dati saranno copiati. In questo caso, è l'indirizzo virtuale vaddr passato alla funzione.
    iov_len: la lunghezza dello spazio di memoria disponibile in cui i dati possono essere copiati. Qui, è memsize, che rappresenta quanto spazio di memoria è stato riservato per il segmento.

Struttura uio (u)

    uio_iov: un puntatore a una struttura iovec, che descrive lo spazio di memoria in cui i dati saranno copiati.
    uio_iovcnt: numero di strutture iovec (qui, solo 1).
    uio_resid: il numero di byte rimanenti da trasferire, che inizialmente è uguale a filesize, cioè la quantità di dati da leggere dal file.
    uio_offset: l'offset nel file da cui iniziare la lettura.
    uio_segflg: flag che indica se l'operazione coinvolge la memoria utente (UIO_USERSPACE) o la memoria kernel (UIO_SYSSPACE). Qui è UIO_SYSSPACE, perché si sta caricando in memoria kernel.
    uio_rw: specifica l'operazione da eseguire, in questo caso UIO_READ per leggere dal file.
    uio_space: utilizzato in operazioni che coinvolgono lo spazio degli indirizzi utente, ma qui è NULL perché si opera nel kernel.
    */

#if OPT_PROJECT
static int load_elf_page(struct vnode *v,
         off_t offset, vaddr_t vaddr,
         size_t memsize, size_t filesize)
{

    struct iovec iov;
    struct uio u;
    int result;

    if (filesize > memsize) {
        kprintf("ELF: avviso: dimensione del file del segmento > dimensione della memoria del segmento\n");
        filesize = memsize;
    }

    DEBUG(DB_VM,"ELF: Caricamento di %lu byte in 0x%lx\n",(unsigned long) filesize, (unsigned long) vaddr);

    /**
     * Non possiamo utilizzare uio_kinit, poiché non consente di impostare un valore diverso per iov_len e uio_resid (che è fondamentale per noi. Vedi testbin/zero per ulteriori dettagli).
    */

    iov.iov_ubase = (userptr_t)vaddr;
    iov.iov_len = memsize;		
    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = filesize;          
    u.uio_offset = offset;
    u.uio_segflg = UIO_SYSSPACE;
    u.uio_rw = UIO_READ;
    u.uio_space = NULL;

    result = VOP_READ(v, &u);

    return result;
}

int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    int swap_found, result;
    struct addrspace *as;
    int sz=PAGE_SIZE, memsz=PAGE_SIZE;
    size_t additional_offset=0;

    swap_found = load_swap(vaddr, pid, paddr); //controlliamo se la pagina è già stata letta dal file elf, cioè se è attualmente memorizzata nel file di swap.

    if(swap_found){
        return 0; 
    }

    as = proc_getas();


	#if OPT_DEBUG
	print_list(pid);
	#endif


    DEBUG(DB_VM,"Processo %d cerca di leggere ELF\n",pid);
    /**
     * Verifichiamo se l'indirizzo virtuale fornito appartiene al segmento di testo del processo
    */
    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){

        DEBUG(DB_VM,"\nCARICAMENTO CODICE: ");

        add_page_fault(DISK_FAULT);

        /**
         * Alcuni programmi potrebbero avere l'inizio del segmento di testo/dati non allineato a una pagina (vedi testbin/bigfork come riferimento). Questa informazione viene persa in as_create, poiché
         * as->as_vbase contiene un indirizzo allineato a una pagina, controlliamo anche che sia la prima pagina che stiamo aggiungendo . Per risolvere il problema, dobbiamo aggiungere un campo aggiuntivo nella struttura as, offset. L'offset viene utilizzato quando accediamo alla
         * prima pagina se è diverso da 0. In questo caso, riempiamo la pagina con zeri e iniziamo a caricare l'ELF con additional_offset come offset all'interno del frame.
         * Questo blocco gestisce la situazione in cui la prima pagina di un segmento deve essere caricata, (controllo sia la prima pagina) ma il segmento non è allineato al bordo della pagina di memoria. La pagina viene zero-fillata, poi viene applicato un offset per caricare correttamente i dati
        */
        if(as->initial_offset1!=0 && (vaddr - as->as_vbase1)==0){
            bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
            additional_offset=as->initial_offset1; //Altrimenti è 0
            if(as->ph1.p_filesz>=PAGE_SIZE-additional_offset){
                sz=PAGE_SIZE-additional_offset; //filesz è abbastanza grande da riempire la parte rimanente del blocco, quindi carichiamo PAGE_SIZE-additional_offset byte
            }
            else{
                sz=as->ph1.p_filesz; //filesz non è abbastanza grande da riempire la parte rimanente del blocco, quindi carichiamo solo filesz byte.
            }
        }
        else{

            if(as->ph1.p_filesz+as->initial_offset1 - (vaddr - as->as_vbase1)<PAGE_SIZE){ //Se filesz non è abbastanza grande da riempire l'intera pagina, dobbiamo riempirla con zeri prima di caricare i dati.
                bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);//Per evitare ulteriori fault TLB, fingiamo che l'indirizzo fisico fornito appartenga al kernel. In questo modo, la traduzione degli indirizzi sarà semplicemente vaddr-0x80000000.
                sz=as->ph1.p_filesz+as->initial_offset1 - (vaddr - as->as_vbase1);//Calcoliamo la dimensione del file dell'ultima pagina (si noti che dobbiamo tenere conto anche di as->initial_offset).
                memsz=as->ph1.p_memsz+as->initial_offset1 - (vaddr - as->as_vbase1);//Calcoliamo la dimensione della memoria dell'ultima pagina (si noti che dobbiamo tenere conto anche di as->initial_offset).
            }

            if((int)(as->ph1.p_filesz+as->initial_offset1) - (int)(vaddr - as->as_vbase1)<0){//Questo controllo è fondamentale per evitare problemi con i programmi che hanno filesz<memsz. Infatti, senza questo controllo non azzereremmo la pagina, causando errori. Per una comprensione più approfondita, prova a eseguire il debug di testbin/zero analizzando la differenza tra memsz e filesz.
                bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
                DEBUG(DB_VM,"CARICA ELF in 0x%x (virtuale: 0x%x) per il processo %d\n",paddr, vaddr, pid);
                return 0;//Ritorniamo direttamente per evitare di eseguire una lettura di 0 byte
            }
        }

        DEBUG(DB_VM,"CARICA ELF in 0x%x (virtuale: 0x%x) per il processo %d\n",paddr, vaddr, pid);


        result = load_elf_page(as->v, as->ph1.p_offset+(vaddr - as->as_vbase1), PADDR_TO_KVADDR(paddr+additional_offset), memsz, sz-additional_offset);//Carichiamo la pagina
        if (result) {
            panic("Errore durante la lettura del segmento di testo");
        }

        add_page_fault(ELF_FAULT);

        return 0;
    }

    /**
     * Verifichiamo se l'indirizzo virtuale fornito appartiene al segmento dei dati. Utilizziamo la stessa logica vista per il segmento di testo.
    */
    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){

        DEBUG(DB_VM,"\nCARICAMENTO DATI, virtuale = 0x%x, fisico = 0x%x\n",vaddr,paddr);

        add_page_fault(DISK_FAULT);

        if(as->initial_offset2!=0 && (vaddr - as->as_vbase2)==0){
            bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
            additional_offset=as->initial_offset2;
            if(as->ph2.p_filesz>=PAGE_SIZE-additional_offset){
                sz=PAGE_SIZE-additional_offset;
            }
            else{
                sz=as->ph2.p_filesz;
            }
        }
        else{
            if(as->ph2.p_filesz+as->initial_offset2 - (vaddr - as->as_vbase2)<PAGE_SIZE){ 
                bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
                sz=as->ph2.p_filesz+as->initial_offset2 - (vaddr - as->as_vbase2);
                memsz=as->ph2.p_memsz+as->initial_offset2 - (vaddr - as->as_vbase2);
            }

            if((int)(as->ph2.p_filesz+as->initial_offset2) - (int)(vaddr - as->as_vbase2)<0){
                bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
                add_page_fault(ELF_FAULT);
                DEBUG(DB_VM,"CARICA ELF in 0x%x (virtuale: 0x%x) per il processo %d\n",paddr, vaddr, pid);
                return 0;
            }
        }

        result = load_elf_page(as->v, as->ph2.p_offset+(vaddr - as->as_vbase2),	PADDR_TO_KVADDR(paddr+additional_offset),memsz, sz);
        if (result) {
            panic("Errore durante la lettura del segmento dei dati");
        }

        add_page_fault(ELF_FAULT);

        DEBUG(DB_VM,"CARICA ELF in 0x%x (virtuale: 0x%x) per il processo %d\n",paddr, vaddr, pid);

        return 0;
    }

    /**
     * Verifichiamo se l'indirizzo virtuale fornito appartiene al segmento del testo.
     * Il controllo viene effettuato in questo modo poiché lo stack cresce verso l'alto da 0x80000000 (escluso) a as->as_vbase2 + as->as_npages2 * PAGE_SIZE
    */
    if(vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE && vaddr<USERSTACK){

        DEBUG(DB_VM,"\nCARICAMENTO STACK: ");

        DEBUG(DB_VM,"ELF: Caricamento di 4096 byte in 0x%lx\n",(unsigned long) vaddr);

        //questa volta riempiamo semplicemente la pagina con zeri, quindi non c'è bisogno di eseguire alcun tipo di caricamento.
        bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
        
        add_page_fault(ZEROED_FAULT);//aggiorniamo le statistiche

        DEBUG(DB_VM,"CARICA ELF in 0x%x (virtuale: 0x%x) per il processo %d\n",paddr, vaddr, pid);

        return 0;
    }

    /**
     * Errore (accesso al di fuori dello spazio degli indirizzi)
     * Termina il programma per accesso non consentito
    */
    kprintf("SEGNALAZIONE DI SEGMENTAZIONE: il processo %d ha acceduto a 0x%x\n",pid,vaddr);

    sys__exit(-1);

    return -1;
    
    
}
#endif
