#ifndef _VM_TLB_H_
#define _VM_TLB_H_

/*
* struttura dati per la gestione della TLB
*/
struct tlb{
    /* data */
};

/*
* funzione per rimuovere una entry dalla tlb 
* in caso di miss. 
* // TODO: CLAPE
* @return  indice entry rimossa, 
* -1 in caso di errore
*/
int tlb_remove(void);

#endif /* _VM_TLB_H_ */