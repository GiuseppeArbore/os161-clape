#ifndef _VM_COREMAP_H_
#define _VM_COREMAP_H_

int *bitmapFreeFrames; // bitmap tieni traccia dei frame liberi
int bitmapFreeFramesActive = 0; // flag per sapere se bitmap è attivo


/*
* Questa è usata per ottenere un frame libero   
*
* @return l'indice del frame libero
*/
int get_frame(void);

/*
* Questa è usata per liberare un frame
*
* @param frame l'indice del frame da liberare
*/
void free_frame(int);

/*
* Questa è usata per inizializzare la bitmap
*/
void bitmap_init(void);

/*
* Questa è usata per distruggere la bitmap
*/
void bitmap_destroy(void);

/*
* Questa è usata per sapere se la bitmap è attiva
*/
int bitmap_is_active(void);


#endif /* _VM_COREMAP_H_ */
