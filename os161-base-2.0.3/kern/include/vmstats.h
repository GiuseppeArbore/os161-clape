#ifndef _VMSTATS_H_
#define _VMSTATS_H_

#include <types.h>
#include <lib.h>
#include <spinlock.h>

/* COSTANTI PER I TIPI DI FAULT */
#define  FREE_FAULT 0
#define  REPLACE_FAULT 1


/*
* Struttura dati per salvare statistiche
*/

//todo: aggiungere spinlock se serve
struct stats
{
    uint32_t tlb_faults; //numero di miss TLB totali (non inclusi fault che causano crash)
    uint32_t tlb_free_faults; //numero di page faults per cui c'era spazio libero nella TLB per aggiungere la entry (no replacement)
    uint32_t tlb_replace_faults; //numero di page faults per cui è stato necessario fare replacement
    uint32_t tlb_invalidations; //numero di invalidazioni della intera TLB (non il numero di entry invalidata)
    uint32_t tlb_reloads; // numero di miss TLB per pagine già presenti in memoria
    uint32_t page_zeroed_faults; //numero di page faults per cui è stato necessaria una nuova pagina inizializzata a 0
    uint32_t page_disk_faults; //numero di page faults per cui è stato necessaria una nuova pagina caricata da disco
    uint32_t page_elf_faults; //numero di page faults per cui è stato necessario caricare una pagine da un file ELF
    uint32_t page_swapfile_faults; //numero di page faults per cui è stato necessario caricare una pagina dallo swapfile
    uint32_t swapfile_writes; //numero di page fault per cui è stato necessario scrivere una pagina sullo swapfile
};



/*
* funzione per inizializzare le statistiche
*/
void stats_init(void);

/*
* funzione per gestire statistiche dei fault della TLB (fault, free_faults, replace_faults)
*
* @param: tipo di fault
*/
void stats_tlb_fault(int faulttype);

/*
* funzione per gestire statistiche delle invalidazioni della TLB
*/
void stats_tlb_invalidation(void);

/* 
* funzione per gestire statistiche dei reload della TLB
*/
void stats_tlb_reload(void);

/*
* funzione per gestire le statistiche page faults (Zeroed, Disk, ELF, Swapfile)
*
* @param: tipo di page fault
*/
void stats_page_fault(int faulttype);

/*  
* funzione per gestitr le statistiche delle scritture deòòe swapfile
*/
void stats_swapfile_write(void);

/*UTILITY FUNCTIONS*/
void add_tlb_fault(void);
void add_tlb_type_fault(int faulttype);
void add_tlb_invalidation(void);
void add_tlb_reload(void);

#endif /* _VMSTATS_H_ */