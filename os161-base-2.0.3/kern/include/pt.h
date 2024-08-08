#ifndef _PT_H_
#define _PT_H_

#include <types.h>


int pt_active; // flag per sapere se la page table è attiva

/*
* Struttura dati per gestire la page table
*/
struct pt_entry{
    vaddr_t vaddr; // indirizzo virtuale
    paddr_t paddr; // indirizzo fisico
    int valid; // flag per sapere se la pagina è valida
    int dirty; // flag per sapere se la pagina è stata modificata
    int used; // flag per sapere se la pagina è stata usata
    int swap; // flag per sapere se la pagina è nello swapfile
    int elf; // flag per sapere se la pagina è stata caricata da un file ELF
    int pid; // pid del processo a cui appartiene la pagina
};

/*
* struttura dati per informazioni riguardanti la page table
*/
struct pt_info{
    struct pt_entry *entries; // array di pt_entry    (PT) 
    int n_entry; // numero di entry nella page table
    paddr_t first_free_paddr; // primo indirizzo fisico libero
    //int *contiguous;    // array di flag per sapere se le pagine sono contigue
    //todo: aggiungeere eventuale lock e cv

}; 

/*
* Questa funzione inizializza la page table
*/
void pt_init(void);


/* 
* Questa funzione converte un indirizzo logico in un indirizzo fisico 
*
* @param pid del processo che chiede per la page table
* 
* @return NULL se la page table non esiste nella page table, altrimenti l'indirizzo fisico
*/
paddr_t pt_get_paddr(vaddr_t, pid_t);

/*
* Questa funzione carica una nuova pagina dall'elf file.
* Se la page table è piena, selezion la paginna da rimuovere
* usando l'algoritmo second-chance e lo salva nell swip file
*
* @param pid del processo che chiede per la page table
* @param l'indirizzo virtuale della pagina da caricare
*
* @return NULL in caso di errore, altrimenti l'indirizzo fisico
*/
paddr_t pt_load_page(vaddr_t, pid_t);

/*
* Questa funzione rimuove tutte le pagine associate ad un processo quando termina
*
* @param pid del processo che termina
*
* @return 0 in caso di successo, -1 in caso di errore
*/
int free_pages(pid_t);

#endif /* _PT_H_ */