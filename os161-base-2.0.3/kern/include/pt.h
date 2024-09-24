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
int nkmalloc; 
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
} page_table; 


/**
 * singola entry della hash table
 */
struct hash_entry{
    int ipt_entry;      //puntatore a entry IPT
    vaddr_t vad;        //indirizzo virtuale
    pid_t pid;          //pid del processo della entry
    struct hash_entry *next;    //puntatore alla prossima entry della hash
};

/**
 * struttura della hash_table
 */
struct hash_table{
    struct hash_entry **table;  //array di puntatori a entry della hash con dimensione size
    int size;
} htable;

/*
    lista dove sono memorizzati tutti i blocchi non utilizzati per la hash table
*/
struct hash_entry *unused_ptr_list; 


/**
*    inizializza la page table
*
*    @param void

*    @return void
*/
void pt_init(void);


/**
* Converte un indirizzo logico in un indirizzo fisico 
*
* @param: indirizzo virtuale che vogliamo accedere
* @param: pid del processo che chiede per la page table
*
* @return -1 se la page table non esiste nella page table, altrimenti l'indirizzo fisico
*/
int pt_get_paddr(vaddr_t, pid_t);

/**
 * funzione per trovare vittima nella page table
 * 
 * @param: indirizzo virtuale che vogliamo accedere
 * @param: pid del processo che chiede per la page table
 * 
 * @return: indirizzo fisico trovato nella IPT
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

/**
 * Questa funzione avvisa che un frame (indirizzo virtuale) è stato rimosso dalla TLB.
 *
 * @param vaddr_t: indirizzo virtuale
 *
 * @return 1 se tutto è ok, -1 altrimenti
 */
int update_tlb_bit(vaddr_t, pid_t);


/**
 * funzione per inserire nella IPT della memoria kernel in modo contiguo
 * 
 * @param int:  numero di pagine contigue da allocare
 * 
 * @return indirizzo fisico trovato nella ipt
 */
paddr_t get_contiguous_pages(int);



/**
 * funzione per liberare lo pagine contigue allocate nella ipt
 * 
 * @param vaddr_t: indirizzo virtuale
 * 
 * @return void
 */
void free_contiguous_pages(vaddr_t);

void print_nkmalloc(void);

/**
 * Questa funzione viene utilizzata per copiare all'interno della PT o del file di swap tutte le pagine del vecchio pid per il nuovo
 *
 * @param pid_t: pid del processo sorgente
 * @param pid_t: pid del processo da agiungere ad ogni pagina
 * 
 * @return void
 */
void copy_pt_entries(pid_t, pid_t);


/**
 *  setta a uno tutti i bit SWAP relativi al pid passato
 * 
 * @param pid_t: pid del processo
 * 
 * @return void
 */
void prepare_copy_pt(pid_t);


/**
 *  setta a zero tutti i bit SWAP relativi al pid passato
 * 
 * @param pid_t: pid del processo
 * 
 * @return void
 */ 

void end_copy_pt(pid_t);


/**
 * funzione per inizializzare la page table
 * 
 * @param void
 * 
 * @return void
 */
void hashtable_init(void);

/**
 * funzione per aggiungere un blocco alla hash table prendendolo da unused_ptr_list
 * 
 * @param vaddr_t: indirizzo virtuale
 * @param pid_t: pid del processo
 * @param int: indice nella page table
 * 
 * @return void
 */
void add_in_hash(vaddr_t, pid_t, int);


/**
 * funzione per ottenere l'indice della hash table
 * 
 * @param vaddr_t: indirizzo virtuale
 * @param pid_t: pid del processo
 * 
 * @return indice nella hash table
 */
int get_index_from_hash(vaddr_t, pid_t);

/**
 * rimuove una lista di blocchi dalla page_Table e la aggiunge alla lista di blocchi liberi
 * 
 * @param vaddr_t: indirizzo virtuale
 * @param pid_t: pid del processo
 * 
 * @return void
 */
void remove_from_hash(vaddr_t, pid_t);

/**
 * calcola l'entry della hash table usando una funzione di hash
 * 
 * @param vaddr_t: indirizzo virtuale
 * @param pid_t: pid del processo
 * 
 * @return entry nella hash table
 */
int get_hash_func(vaddr_t, pid_t);

#endif /* _PT_H_ */
