// page tables e manipolazione delle entry della page table
#include "pt.h"
#include "vmstats.h"
#include <types.h>
#include <vm.h>
#include <lib.h>


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
struct pt{
    struct pt_entry *entries; // array di pt_entry    (PT) 
    int n_entry; // numero di entry nella page table
    paddr_t first_free_paddr; // primo indirizzo fisico libero

}; 

struct pt *page_table; // page table

/* TODOOOOO
* Questa funzione inizializza la page table, SISTEMARE
*/

void pt_init(void){
    page_table = kmalloc(sizeof(struct pt));
    if(page_table == NULL){
        panic("Errore nell'allocazione della page table");
    }
    page_table->n_entry = 0;
    page_table->first_free_paddr = 0;
    page_table->entries = NULL;
    pt_active = 1;
}
