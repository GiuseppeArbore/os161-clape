#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include "types.h"
#include "syscall.h"
#include "proc.h"
#include "opt-debug.h"


/*
* struttura dati per la gestione della TLB
*/
struct tlb{
  
};


/*
*Questa è una funzione che trova la vittima
* @return indice della vittima
*/
int tlb_victim(void);

//riguardare non sono sicura
/* 
*Questa funzione mi dice se l'indirizzo è nel text segment e qualora non sia scrivibile
* @param indirizzo virtuale
* @return 1 se è nel text segment e non scrivibile, 0 altrimenti
*/
int segment_is_readonly(vaddr_t vaddr);

/*
* Questa funzione inserisce l'indirizzo del fault nella TLB con il corrispondente indirizzo fisico
*@param indirizzo virtuale
*@param indirizzo fisico

*/
int tlb_insert(vaddr_t vaddr, paddr_t paddr);

/*
* Questa funzione mi dice se laentry della tlb è valids 
* il valid bit 
*/
int tlb_entry_is_valid(int index);

/*
* Questa funzione invalida le entry corrispondenti all'indirizzo fisico 
* Utile quando una entry nella pt è invalidata e bisogna invalidare anche la corrispondente entry nella tlb
* @param indirizzo fisico
* @return 0 in caso di successo, -1 in caso di errore
*/
 int tlb_invalidate_entry(paddr_t paddr);

 /* 
 *Questa funzione è usata per invalidare la TLB quando c'è uno switch da un processo all'altro
 * La TLB è comune per tutti i processi e non ha un campo "pid"
 */
void tlb_invalidate_all(void);

#endif /* _VM_TLB_H_ */