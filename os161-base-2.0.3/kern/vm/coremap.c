// tiene traccia dei frame fisici liberi
#include "coremap.h"
#include <types.h>
#include <lib.h>
#include <vm.h>
#include <mainbus.h>
#include <spinlock.h>


static int nframes=0; // numero di frame fisici
static int bitmapFreeFrames_active = 0; // flag per sapere se la bitmap è attiva
/*
* definisco spinlock per la bitmap
*/
static struct spinlock bitmapLock = SPINLOCK_INITIALIZER;

/*
* Funzione per iniziaizzare la bitmap
*/
void bitmap_init(void){
    nframes = mainbus_ramsize() / PAGE_SIZE ; // calcola il numero di frame fisici

    bitmapFreeFrames = kmalloc(nframes * sizeof(int)); // alloca la bitmap
    if(bitmapFreeFrames == NULL){
        panic("Errore nell'allocazione della bitmap");
    }
    for(int i=0; i<nframes; i++){
        bitmapFreeFrames[i] = 0; // inizializza la bitmap
    }

    spinlock_acquire(&bitmapLock); // acquisisce il lock
    bitmapFreeFramesActive = 1; // attiva la bitmap
    spinlock_release(&bitmapLock); // rilascia il lock
}


/*
* Funzione per distruggere la bitmap
*/
void bitmap_destroy(void){
    spinlock_acquire(&bitmapLock); // acquisisce il lock
    bitmapFreeFramesActive = 0; // disattiva la bitmap
    spinlock_release(&bitmapLock); // rilascia il lock
    kfree(bitmapFreeFrames); // dealloca la bitmap
}

/*
* Funzione per verificarr se la bitmap è attiva
*/
int bitmap_is_active(void){
    return bitmapFreeFramesActive;
}