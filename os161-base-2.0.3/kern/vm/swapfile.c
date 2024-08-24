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

    return 0;//Non trovi la entry nel swapfile, quindi ritorniamo 0
}

int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    int result;
    struct iovec iov;
    struct uio ku;

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

}

int swap_init(void){
    int result;
    int i;
    char fname[9];

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

    swap->elements = kmalloc(swap->size*sizeof(struct swap_cell));

    if(!swap->elements){
        panic("Error during swap elements allocation");
    }


    for(i=(int)(swap->size-1); i>=0; i--){//Create all the elements in the free list. We iterate in reverse order because we perform head insertion, and in this way the first free elements will have small offsets.
        swap->elements[i].pid=-1;//We mark all the pages of the swapfile as free

    }

    return 0;
}

void remove_process_from_swap(pid_t pid){
    int i;
    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==pid){
            occ--;
            DEBUG(DB_VM,"Process %d released. Now occ=%d\n",curproc->p_pid,occ);
            swap->elements[i].pid=-1;
        }
    }
}