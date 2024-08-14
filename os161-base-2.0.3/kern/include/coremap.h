#ifndef _VM_COREMAP_H_
#define _VM_COREMAP_H_

#include <types.h>

/*
* bitmap che tiene traccia dei frame fisici liberi
*/
int *bitmapFreeFrames; // bitmap tieni traccia dei frame liberi


/*
* funzione usata per ottenere un frame libero   
*
* @return l'indice del frame libero
*/
int get_frame(void);

/*
* funzione usata per liberare un frame
*
* @param frame l'indice del frame da liberare
*/
void free_frame(int);

/*
* Qfunzione usata per inizializzare la bitmap
*/
void bitmap_init(void);

/*
* funzione usata per distruggere la bitmap
*/
void bitmap_destroy(void);

/*
* funzione usata per sapere se la bitmap è attiva
*/
int bitmap_is_active(void);


#endif /* _VM_COREMAP_H_ */
