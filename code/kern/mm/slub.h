#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* Initialize after pmm_init(); repeated calls preserve existing allocations. */
void slub_init(void);

/* Return at least 16-byte-aligned storage, or NULL for zero size/failure. */
void *slub_alloc(size_t size);

/* Accept NULL or a live object returned by slub_alloc(). */
void slub_free(void *ptr);

#endif /* !__KERN_MM_SLUB_H__ */
