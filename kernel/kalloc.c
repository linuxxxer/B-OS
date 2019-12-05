// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  // pole, kazdy prvok zodpovedna jednemu ramcu a uchovava pocet referencii na dany ramec
  uint64 *cntref;
} kmem;

void
kinit()
{
    int frames = 0;
    uint64 addr = PGROUNDUP((uint64)(end));

    kmem.cntref = (uint64*)addr;
    while(addr < PHYSTOP)
    {
        kmem.cntref[PA2IND(addr)] = 1;
        addr += PGSIZE;
        frames++;
    }

  initlock(&kmem.lock, "kmem");
  freerange(kmem.cntref+frames, (void*)PHYSTOP);
}

// dekrementacia poctu referencii pre dany ramec
// vratim novy pocet referencii pre dany ramec
int dec_ref(void * pa)
{
    acquire(&kmem.lock);
    // ak je ramec volny, tak doslo k chybe jadra (nemozem drekementovat)
    if (kmem.cntref[PA2IND(pa)] == 0)
        panic("dec_ref: cntref zero");
    kmem.cntref[PA2IND(pa)]--;
    int ref_num = kmem.cntref[PA2IND(pa)];
    release(&kmem.lock);
    return ref_num;
}

// inkrementacia bez pouzitia zamku, pouzit v pripade ak uz mam ziskany zamok
void inc_ref_interval(void * pa)
{
    kmem.cntref[PA2IND(pa)]++;
}


// TODO: kontrolova pretecenia pocitadla referencii
// inkrementacia poctu referencii pre dany ramec
void inc_ref(void * pa)
{
    acquire(&kmem.lock);
    inc_ref_interval(pa);
    release(&kmem.lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // ak je pocet refenrecii na ramec vacsi ako 0, tak nebuden uvolnovat
  // no ak je pocet referencii nula, tak mozem uvolnit
  if(dec_ref(pa) == 0)
  {
      // Fill with junk to catch dangling refs.
      memset(pa, 1, PGSIZE);

      r = (struct run*)pa;

      acquire(&kmem.lock);
      r->next = kmem.freelist;
      kmem.freelist = r;
      release(&kmem.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void) {
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
        // po alokovani PA treba zvysit jej pocet referencii
        inc_ref_interval(r);
    }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
