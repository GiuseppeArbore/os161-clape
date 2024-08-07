

char *bitmapFreeFrames; // bitmap tieni traccia dei frame liberi

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
