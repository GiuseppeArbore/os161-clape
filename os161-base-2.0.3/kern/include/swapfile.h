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

/*
*  Struttura per caricare l'associazione
* (indirizzo virtuale - pid) -> posizione swapfile
*/
struct swapfile{
    struct swap_cell *elements;
    struct vnode *v;
    size_t size;

};

struct swap_cell{
    pid_t pid;
    vaddr_t vaddr;
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




#endif /* _SWAPFILE_H_ */