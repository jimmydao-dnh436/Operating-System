
/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   int numstep = 0;

   mp->cursor = 0;
   while (numstep < offset && numstep < mp->maxsz)
   {
      /* Traverse sequentially */
      mp->cursor = (mp->cursor + 1) % mp->maxsz;
      numstep++;
   }

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE)mp->storage[addr];

   return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg)
      *value = mp->storage[addr];
   else /* Sequential access device */
      return MEMPHY_seq_read(mp, addr, value);

   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{

   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   mp->storage[addr] = value;

   return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg)
      mp->storage[addr] = data;
   else /* Sequential access device */
      return MEMPHY_seq_write(mp, addr, data);

   return 0;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 *  @pagesz: page size in bytes
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   /* This setting come with fixed constant PAGESZ */
   int numfp = mp->maxsz / pagesz;
   struct framephy_struct *newfst, *fst;
   int iter = 0;

   if (numfp <= 0)
      return -1;

   /* Init head of free framephy list */
   fst = malloc(sizeof(struct framephy_struct));
   fst->fpn = iter;
   /* fp_next of head must be NULL before the loop links further nodes,
    * otherwise the tail of the list holds a garbage pointer */
   fst->fp_next = NULL;
   mp->free_fp_list = fst;

   /* We have list with first element, fill in the rest num-1 element member*/
   for (iter = 1; iter < numfp; iter++)
   {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }

   return 0;
}

/*
 *  MEMPHY_get_freefp - obtain a free physical frame from MEMPHY
 *  @mp: memphy struct
 *  @retfpn: obtained frame page number
 */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   struct framephy_struct *fp = mp->free_fp_list;

   if (fp == NULL)
      return -1;

   /* Detach head node from free list and return its frame number */
   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;

   /* MEMPHY is iteratively used up until its exhausted
    * No garbage collector acting then it not been released
    */
   /* Move node to used list instead of freeing it so the set of
    * in-use frames can be walked by MEMPHY_dump and correctly
    * reclaimed by MEMPHY_put_freefp later */
   fp->fp_next = mp->used_fp_list;
   mp->used_fp_list = fp;

   return 0;
}

/*
 *  MEMPHY_dump dump memphy content mp->storage
 *  @mp: memphy struct
 */
int MEMPHY_dump(struct memphy_struct *mp)
{
   /*TODO dump memphy contnt mp->storage
    *     for tracing the memory content
    */

   if (mp == NULL)
      return -1;

   int pagesz    = PAGING_PAGESZ;
   int numframes = mp->maxsz / pagesz;

   /* Print device-level summary before walking frames */
   printf("\n=========== MEMPHY DUMP ===========\n");
   printf("  Capacity : %d bytes  (%d frames x %d B/frame)\n",
          mp->maxsz, numframes, pagesz);
   printf("  Access   : %s\n", mp->rdmflg ? "random" : "sequential");

   /* Walk free_fp_list to report which frames are still available */
   printf("  Free frames : ");
   struct framephy_struct *cur = mp->free_fp_list;
   int cnt = 0;
   while (cur != NULL)
   {
      // printf("%d ", cur->fpn);
      printf("%llu ", (unsigned long long)cur->fpn);
      cur = cur->fp_next;
      cnt++;
   }
   if (cnt == 0)
      printf("(none)");
   printf("  [%d total]\n", cnt);

   /* Walk used_fp_list to report which frames are currently allocated */
   printf("  Used frames : ");
   cur = mp->used_fp_list;
   cnt = 0;
   while (cur != NULL)
   {
      // printf("%d ", cur->fpn);
      printf("%llu ", (unsigned long long)cur->fpn);
      cur = cur->fp_next;
      cnt++;
   }
   if (cnt == 0)
      printf("(none)");
   printf("  [%d total]\n", cnt);

   /* Dump raw bytes of every frame as hex and printable ASCII,
    * matching the layout convention used in memory-trace tools */
   printf("\n  --- frame storage (hex | ASCII) ---\n");
   int frm, col;
   BYTE b;
   for (frm = 0; frm < numframes; frm++)
   {
      int base = frm * pagesz;
      printf("  frame %4d [0x%06x] : ", frm, base);

      /* Hex column – show up to 16 bytes per frame line */
      for (col = 0; col < pagesz && col < 16; col++)
      {
         b = mp->storage[base + col];
         printf("%02x ", b);
      }
      if (pagesz > 16)
         printf("... ");

      printf("| ");

      /* ASCII column – non-printable bytes replaced with '.' */
      for (col = 0; col < pagesz && col < 16; col++)
      {
         b = mp->storage[base + col];
         printf("%c", (b >= 0x20 && b <= 0x7e) ? (char)b : '.');
      }
      if (pagesz > 16)
         printf("...");

      printf("\n");
   }

   printf("====================================\n\n");
   return 0;
}

/*
 *  MEMPHY_put_freefp - return a used frame back to free pool
 *  @mp: memphy struct
 *  @fpn: frame page number to return
 */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   /* Search used_fp_list for the matching frame node so we
    * can reuse the same allocation rather than leaking it */
   struct framephy_struct *fp   = mp->used_fp_list;
   struct framephy_struct *prev = NULL;
   struct framephy_struct *node = NULL;

   while (fp != NULL)
   {
      if ((addr_t)fp->fpn == fpn)
      {
         /* Unlink from used list before moving to free list */
         if (prev != NULL)
            prev->fp_next    = fp->fp_next;
         else
            mp->used_fp_list = fp->fp_next;

         node = fp;
         break;
      }
      prev = fp;
      fp   = fp->fp_next;
   }

   /* Create new node with value fpn */
   /* If frame was not tracked in used list, allocate a fresh node
    * so the frame is still correctly returned to the free pool */
   if (node == NULL)
   {
      node      = malloc(sizeof(struct framephy_struct));
      node->fpn = fpn;
   }

   /* Prepend to free list – mirrors the original single-line logic
    * of pointing the new node's next at the current list head */
   node->fp_next    = mp->free_fp_list;
   mp->free_fp_list = node;

   return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;
   memset(mp->storage, 0, max_size * sizeof(BYTE));

   MEMPHY_format(mp, PAGING_PAGESZ);

   /* used_fp_list has no entries at startup; set to NULL explicitly
    * so MEMPHY_get_freefp and MEMPHY_dump see a well-formed list */
   mp->used_fp_list = NULL;

   mp->rdmflg = (randomflg != 0) ? 1 : 0;

   if (!mp->rdmflg) /* Not Ramdom acess device, then it serial device*/
      mp->cursor = 0;

   return 0;
}

// #endif