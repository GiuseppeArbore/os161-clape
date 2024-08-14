//codice per tracciare statistiche
#include "vmstats.h"

//inizializza le statistiche
void stats_init(void)
{
    stats.tlb_faults = 0;
    stats.tlb_free_faults = 0;
    stats.tlb_replace_faults = 0;
    stats.tlb_invalidations = 0;
    stats.tlb_reloads = 0;
    stats.page_zeroed_faults = 0;
    stats.page_disk_faults = 0;
    stats.page_elf_faults = 0;
    stats.page_swapfile_faults = 0;
    stats.swapfile_writes = 0;
}

//gestisce le statistiche dei fault della TLB
void add_tlb_fault(int faulttype)
{
    stats.tlb_faults++;
    switch (faulttype)
    {
    case FREE_FAULT:
        stats.tlb_free_faults++;
        break;
    case REPLACE_FAULT:
        stats.tlb_replace_faults++;
        break;
    default:
        break;
    }

}

//gestisce le statistiche delle invalidazioni della TLB
void stats_tlb_invalidation(void)
{
    stats.tlb_invalidations++;
}

//gestisce le statistiche dei reload della TLB
void stats_tlb_reload(void)
{
    stats.tlb_reloads++;
}

/*
*gestisce le statistiche dei page faults
*/
//TODO capire se serve il singolo DISK_FAULT
void stats_page_fault(int faulttype)
{
    switch (faulttype)
    {
    case ZEROED_FAULT:
        stats.page_zeroed_faults++;
        break;
    case DISK_FAULT:
        stats.page_disk_faults++;
        break;
    case ELF_FAULT:
        stats.page_disk_faults++;
        stats.page_elf_faults++;
        break;
    case SWAPFILE_FAULT:
        stats.page_disk_faults++;
        stats.page_swapfile_faults++;
        break;
    default:
        break;
    }

}

void stats_swapfile_write(void)
{
    stats.swapfile_writes++;
}


/*
* funzione per stampare le statistiche
*/
void stats_print(void)
{
    kprintf("TLB faults: %d\n", stats.tlb_faults);
    kprintf("TLB free faults: %d\n", stats.tlb_free_faults);
    kprintf("TLB replace faults: %d\n", stats.tlb_replace_faults);
    kprintf("TLB invalidations: %d\n", stats.tlb_invalidations);
    kprintf("TLB reloads: %d\n", stats.tlb_reloads);
    kprintf("Page zeroed faults: %d\n", stats.page_zeroed_faults);
    kprintf("Page disk faults: %d\n", stats.page_disk_faults);
    kprintf("Page ELF faults: %d\n", stats.page_elf_faults);
    kprintf("Page swapfile faults: %d\n", stats.page_swapfile_faults);
    kprintf("Swapfile writes: %d\n", stats.swapfile_writes);

    stats_verify();
}

void stats_verify(void){
    if (stats.tlb_faults != stats.tlb_free_faults + stats.tlb_replace_faults)
    {
        kprintf("TLB faults != TLB free faults + TLB replace faults");
    }
    if (stats.tlb_faults != stats.tlb_reloads + stats.page_disk_faults + stats.page_zeroed_faults)
    {
        kprintf("TLB faults != TLB reloads + page disk fault + page zeroed fault");
    }
    if (stats.page_disk_faults != stats.page_elf_faults + stats.page_swapfile_faults)
    {
        kprintf("page disk fault != page elf faults + page swapfile faults");
    }
}

