# Progetto OS161: C1.1
---
### Introduzione
Il progetto ha l'obbiettivo di espandere il modulo della gestione della memoria (dumbvm), sostituendolo completamente con un gestore di memoria virtuale più avanzato basato sulla tabella delle pagine dei progetti. 
Il progetto richiede inoltre di lavorare sulla TLB (Translation Lookaside Buffer).

Il progetto è stato svolto nella variante C1.2 che prevede l'introduzione di una Inverted Page Table con una soluzione per velocizzare la ricerca. Per fare ciò è stata implementata una hash table. 
# CACA VERIFICA QUELLO CHE HO SCRITTO SOPRA

## Composizione e suddivisione del lavoro
---
Il lavoro è stato suddiviso tra i componenti del gruppo nel seguente modo:
- g1: Giuseppe Arbore (s329535): _cosa ho fatto io a grandi linee_
- g2: Claudia Maggiulli (s332252): _cosa hai fatto tu a grandi linee_

Per una miglior coordinazione si è usata una repository condivisa su GitHub e un file condiviso su Notion in modo tale di tener traccia dei vari progressi.

## Implementazione
---

#### Address space (g1):
L'address space è diviso in modo tale da 
__Struttura dati__
```
struct addrspace {
        vaddr_t as_vbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        size_t as_npages2;
        Elf_Phdr ph1;//Program header of the text section
        Elf_Phdr ph2;//Program header of the data section
        struct vnode *v;//vnode of the elf file
        size_t initial_offset1;
        size_t initial_offset2;
        int valid;
}
```



__Implementazione__


___Creazione e distruzione___


___Copia e attivazione___


___Define___


___Find___


#### Page table (g1):
La page table èn strutturata nel seguente modo:
__Struttura dati__
```
struct pt_info{
    struct pt_entry *entries; // array di pt_entry (IPT) 
    int n_entry; // numero di entry nella page table
    paddr_t first_free_paddr; // primo indirizzo fisico libero
    struct lock *pt_lock; 
    struct cv *pt_cv; 
    int *contiguous;    // array di flag per sapere se le pagine sono contigue 
} page_table; 
```
```
struct pt_entry {
    vaddr_t page; // indirizzo virtuale
    pid_t pid; // pid del processo a cui appartiene la pagina
    uint8_t ctrl; // bit di controllo come: validity, reference,isInTlb, ---
};

```


__Implementazione__


___Creazione___

___Copia___

___Cancellazione e distruzione___


___Traduzione di indirizzi___

#### Coremap (g1)
La coremap è una componente fondamentale per la gestione della memoria fisica all'interno del sistema di memoria virtuale. Questa struttura dati tiene traccia dello stato di ogni pagina in memoria fisica, consentendo al sistema di sapere quali pagine sono attualmente in uso, quali sono libere e quali devono essere sostituite o recuperate dal disco. 
__Struttura dati__


__Implementazione__


___Inizializzazione___


___Terminazione___

___Kernel: allocazione e dealocalizzazione pagine___



#### Altre modifiche:


## Test
---
Per verificare l'effettivo funzionamento del sistema, sono stati usati i test già presenti all'interno di os161:
- palin: 
- matmult:
- huge:
- sort:
- forktest:
- bigfork:
- parallelvm:
- ctest:

Inoltre, per verificare le funzioni base del kernel fossero già correttamente implementate, sono stati eseguiti i seguenti test:
- at: 
- at2:
- bt:
- km1:
- km2: 


Di seguito si riportano le statistiche registrate per ogni test:

| Nome test | TLB faults | TLB faults (free) | TLB faults (replace) | TLB invalidations | TLB reloads | Page faults (zeroed) | Page faults (disk) | Page faults (ELF) | Page faults (swapfile) | Swapfile writes |
|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
|palin|||||||||||
|matmult|||||||||||
|huge|||||||||||
|sort|||||||||||
|forktest|||||||||||
|parrelevm|||||||||||
|bigfork|||||||||||
|ctest|||||||||||
|ALTRO|||||||||||



Prima di lanciare i test, è richiesto di aumentare la memoria RAM disponibile a 2MB (per farlo, vedere il file root/sys161.conf) a causa di strutture dati aggiuntive da noi usate. 
# Verificare se funziona anche con 1MB

Per lo swapfile, è stata usare la raw partition di _LHD0.img_ . Nell'implementazione, si è deciso di usare come dimensione 9MB invece dei 5MB presenti nella versione predefinita. Per allinearsi, è quindi richiesto di lanciare il seguente comando all'interno della cartella _root_:

```
disk161 resize LHD0.img 9M
```