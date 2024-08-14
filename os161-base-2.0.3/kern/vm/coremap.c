// tiene traccia dei frame fisici liberi
#include "coremap.h"
#include <types.h>
#include <lib.h>
#include <vm.h>


static int nframes=0; // numero di frame fisici



/*
* Funzione per iniziaizzare la bitmap
*/
void bitmap_init(void){
    nframes = ram_getsize() / PAGE_SIZE; // calcola il numero di frame fisici
    bitmapFreeFrames = kmalloc(nframes * sizeof(int)); // alloca la bitmap
    if(bitmapFreeFrames == NULL){
        panic("Errore nell'allocazione della bitmap");
    }
    for(int i=0; i<nframes; i++){
        bitmapFreeFrames[i] = 0; // inizializza la bitmap
    }
    bitmapFreeFramesActive = 1; // attiva la bitmap
}


/*
* Funzione per distruggere la bitmap
*/
void bitmap_destroy(void){
    bitmapFreeFramesActive = 0; // disattiva la bitmap
    kfree(bitmapFreeFrames); // dealloca la bitmap
}

/*
* Funzione per verificarr se la bitmap è attiva
*/
int bitmap_is_active(void){
    return bitmapFreeFramesActive;
}