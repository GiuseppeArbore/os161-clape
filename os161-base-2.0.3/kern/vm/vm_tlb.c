// codice per manipolare la tlb (incluso replacement)
#include "vm_tlb.h"

#include "vm.h"
#include "vmstats.h"

//todo: implementare le funzioni di manipolazione della tlb
int tlb_remove(void){
    return -1;
}

/*
* usata per gestire il tlb miss
*/
int vm_fault(int faulttype, vaddr_t faultaddress){
    return -1;
}