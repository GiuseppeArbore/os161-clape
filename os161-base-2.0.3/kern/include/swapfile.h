#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

/*
*  Struttura per carcare l'associazione
* (indirizzo virtuale - pid) -> posizione swapfile
*/
struct swapfile{

};

/*
* Questa funzione rimette un frame in RAM
*
* @param indirizzo virtuale che causa il page fault
* @param pid del processo 
* @param indirizzo fisico del frame da usare
*
* @return 0 in caso di successo, -1 in caso di errore
*/
int load_page(vaddr_t, pid_t, paddr_t);

/*
* Questa funzione salva un frame nello swapfile
* Se lo swapfile è maggiore di 90MB, la funzione andrà in kernel panic
*
* @param indirizzo virtuale che causa il page fault
* @param pid del processo
* @param indirizzo fisico del frame da salvare
*
* @return 0 in caso di successo, -1 in caso di errore
*/
int save_page(vaddr_t, pid_t, paddr_t);

#endif /* _SWAPFILE_H_ */