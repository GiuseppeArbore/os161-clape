#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

#include "types.h"
#include "addrspace.h"
#include "kern/fcntl.h"
#include "uio.h"
#include "vnode.h"
#include "copyinout.h"
#include "lib.h"
#include "vfs.h"
#include "vmstats.h"
#include "opt-sw_list.h"
#include "proc.h"
#include "synch.h"
#include "current.h"
#include "vm.h"
#include "spl.h"

/*
*  Struttura per caricare l'associazione
* (indirizzo virtuale - pid) -> posizione swapfile
*/
struct swapfile{
    #if OPT_SW_LIST
     struct swap_cell **text;//Array di liste di pagine di testo nel file di swap (una per ogni pid)
     struct swap_cell **data;//Array di liste di pagine di dati nel file di swap (una per ogni pid)
     struct swap_cell **stack;//Array di liste di pagine di stack nel file di swap (una per ogni pid)
     struct swap_cell *free;//Lista di pagine libere nel file di swap
     //Ogni elemento di questi array è un puntatore alla testa di una lista concatenata di swap_cell per un particolare tipo di segmento (testo, dati, stack) per un processo specifico (pid).
     void *kbuf;//Buffer utilizzato durante la copia delle pagine di swap
     #else
     struct swap_cell *elements;//Array di liste nel file di swap (una per ogni pid)
     #endif
     struct vnode *v;//vnode del file di swap
     int size;//Numero di pagine memorizzate nel file di swap
};

struct swap_cell{
    vaddr_t vaddr;//Indirizzo virtuale corrispondente alla pagina memorizzata
    int store;//Flag che indica se stiamo eseguendo un'operazione di salvataggio su una pagina specifica o meno
    #if OPT_SW_LIST
    struct swap_cell *next;
    paddr_t offset;//Offset dell'elemento di swap all'interno del file di swap
    struct cv *cell_cv;//Utilizzato per attendere la fine dell'operazione di salvataggio
    struct lock *cell_lock;//Necessario per eseguire cv_wait
    #else
    pid_t pid; //Pid del processo a cui appartiene la pagina. Se pid=-1 la pagina è libera
    #endif
};

/**
* Questa funzione rimette un frame in RAM
*
* @param vaddr_t: indirizzo virtuale che causa il page fault
* @param pid_t: pid del processo 
* @param paddr_t: indirizzo fisico del frame da usare
*
* @return 0 in caso di successo, -1 in caso di errore
*/
int load_swap(vaddr_t, pid_t, paddr_t);

/**
* Questa funzione salva un frame nello swapfile
* Se lo swapfile è maggiore di 90MB, la funzione andrà in kernel panic
*
* @param vaddr_t: indirizzo virtuale che causa il page fault
* @param pid_t: pid del processo
* @param paddr_t: indirizzo fisico del frame da salvare
*
* @return 0 in caso di successo, -1 in caso di errore
*/
int store_swap(vaddr_t, pid_t, paddr_t);

/**
 * Questa funzione inizializza il file di swap. In particolare, alloca le strutture dati necessarie e apre il file che conterrà le pagine.
*/
int swap_init(void);

/**
 * Quando un processo termina, segniamo come libere tutte le sue pagine memorizzate nel file di swap.
 * 
 * @param pid_t: pid del processo terminato.
*/
void remove_process_from_swap(pid_t);

/**
 * Quando viene eseguito un fork, copiamo tutte le pagine del vecchio processo anche per il nuovo processo.
 * 
 * @param pid_t: pid del vecchio processo.
 * @param pid_t: pid del nuovo processo.
*/
void copy_swap_pages(pid_t, pid_t);

void reorder_swapfile(void);


#endif /* _SWAPFILE_H_ */