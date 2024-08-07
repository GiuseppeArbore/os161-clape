/*
* Struttura dati per la page table
*/
struct pt{

};

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
