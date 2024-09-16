#ifndef _PT_H_
#define _PT_H_

#include <types.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <synch.h>
#include <spl.h>
#include <opt-debug.h>



int pt_active; // flag per sapere se la page table è attiva
#if OPT_DEBUG
int nkmalloc; //TODO: CLAPE
#endif

/**
* Struttura dati per gestire la page table
*/
struct pt_entry {
    vaddr_t page; // indirizzo virtuale
    pid_t pid; // pid del processo a cui appartiene la pagina
    uint8_t ctrl; // bit di controllo come: validity, reference,isInTlb, ---
};


/**
* struttura dati per informazioni riguardanti la page table
*/
struct pt_info{
    struct pt_entry *entries; // array di pt_entry (IPT) 
    int n_entry; // numero di entry nella page table
    paddr_t first_free_paddr; // primo indirizzo fisico libero
    struct lock *pt_lock; 
    struct cv *pt_cv; 
    int *contiguous;    // array di flag per sapere se le pagine sono contigue
    //todo: CLAPE aggiungeere eventuale lock e cv
 
} page_table; 


/**
 * singola entry della hash table
 */
struct hash_entry{
    int ipt_entry;  
    vaddr_t vad;
    pid_t pid;
    struct hash_entry *next;    
};

/**
 * struttura della hash_table
 */
struct hash_table{
    struct hash_entry **table;
    int size;
} htable;

/*
    list where all unused blocks for the hast table are stored
*/
struct hash_entry *unused_ptr_list; 


/**
* Questa funzione inizializza la page table
*/
void pt_init(void);


/**
* Questa funzione converte un indirizzo logico in un indirizzo fisico 
*
* @param: indirizzo virtuale che voliamo accedere
* @param: pid del processo che chiede per la page table
* @return -1 se la page table non esiste nella page table, altrimenti l'indirizzo fisico
*/
int pt_get_paddr(vaddr_t, pid_t);

/**
 * funzione per trovare vittima nella page table
 * 
 * @param: indirizzo virtuale che vogliamo accedere
 * @param: pid del processo che chiede per la page table
 */
int find_victim(vaddr_t, pid_t);


/**
 * funzione per ottenere la pagina, chiama pt_get_paddr se presente,
 * altrimenti chiama la funzione findspace che cerca spazio libero nella page table
 * Setta il tlb bit ad 1
 * 
 * @param: indirizzo virtuale che vogliamo accedere
 */
paddr_t get_page(vaddr_t);

/**
* Questa funzione carica una nuova pagina dall'elf file.
* Se la page table è piena, selezion la paginna da rimuovere
* usando l'algoritmo second-chance e lo salva nell swip file
*
* @param pid del processo che chiede per la page table
* @param indirizzo virtuale della pagina da caricare
*
* @return NULL in caso di errore, altrimenti l'indirizzo fisico
*/
paddr_t pt_load_page(vaddr_t, pid_t); //TODO: CLAPE: capire se serve

/**
* Questa funzione rimuove tutte le pagine associate ad un processo quando termina
*
* @param pid del processo che termina
*
* @return void
*/
void free_pages(pid_t);


int update_tlb_bit(vaddr_t, pid_t);

paddr_t get_contiguous_pages(int);

void free_contiguous_pages(vaddr_t);

void print_nkmalloc(void);

void copy_pt_entries(pid_t, pid_t);

void prepare_copy_pt(pid_t);

void end_copy_pt(pid_t);

void free_forgotten_pages(void);

/**
 * funzione per inizializzare la page table
 * 
 * @param void
 * @return void
 */
void hashtable_init(void);


void add_in_hash(vaddr_t, pid_t, int);

int get_index_from_hash(vaddr_t, pid_t);

/**
 * 
 */
void remove_from_hash(vaddr_t, pid_t);

/**
 * calcola l'entry della hash table usando una funzione di hash
 * 
 * @param vaddr_t: indirizzo virtuale
 * @param pid_t: pid del processo
 * 
 * @return entry della page table
 */
int get_hash_func(vaddr_t, pid_t);

#endif /* _PT_H_ */