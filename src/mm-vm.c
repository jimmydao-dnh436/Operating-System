/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    int cellidx;
    addr_t ram_addr, swp_addr;
    BYTE ram_data, swp_data;

    /*
     * Old code kept for reference:
     * __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
     * return 0;
     *
     * That version only copied RAM -> SWAP in one direction.
     * The new version swaps both directions so the requested page can be
     * brought back into RAM through the same syscall path.
     */

    if (caller == NULL || caller->krnl == NULL ||
        caller->krnl->mram == NULL || caller->krnl->active_mswp == NULL)
    {
        return -1;
    }

    /* Exchange page contents between one RAM frame and one SWAP frame.
     * This keeps all device access inside kernel-side code after syscall.
     */
    for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
    {
        ram_addr = vicfpn * PAGING_PAGESZ + cellidx;
        swp_addr = swpfpn * PAGING_PAGESZ + cellidx;

        MEMPHY_read(caller->krnl->mram, ram_addr, &ram_data);
        MEMPHY_read(caller->krnl->active_mswp, swp_addr, &swp_data);

        MEMPHY_write(caller->krnl->mram, ram_addr, swp_data);
        MEMPHY_write(caller->krnl->active_mswp, swp_addr, ram_data);
    }

    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  // TODO: update the newrg boundary
  if (cur_vma == NULL)
  {
    return NULL;
  }

  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + alignedsz;
  newrg->rg_next = NULL;
  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  struct vm_area_struct *vma = caller->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1;
  }

  if (vma == NULL)
  {
    return -1;
  }

  /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
  if (cur_area == NULL)
  {
    return -1;
  }

  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(cur_area->vm_start, cur_area->vm_end, vma->vm_start, vma->vm_end))
    {
      return -1;
    }
    vma = vma->vm_next;
  }
  /* End TODO*/

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  /* TOTO with new address scheme, the size need tobe aligned 
   *      the raw inc_sz maybe not fit pagesize
   */
  if (cur_vma == NULL)
  {
    return -1;
  }

  int incnumpage =  DIV_ROUND_UP(inc_sz, PAGING_PAGESZ);
  addr_t aligned_sz = incnumpage * PAGING_PAGESZ;

  struct vm_rg_struct *newrg = get_vm_area_node_at_brk(caller, vmaid, inc_sz, aligned_sz);
  if (newrg == NULL) 
  {
    return -1;
  }

  addr_t old_end = cur_vma->vm_end;
  addr_t new_end = old_end + aligned_sz;

  /* TODO Validate overlap of obtained region */
  if (validate_overlap_vm_area(caller, vmaid, newrg->rg_start, newrg->rg_end) < 0)
   return -1; /*Overlap and failed allocation */

  /* TODO: Obtain the new vm area based on vmaid */
  cur_vma->vm_end = new_end;
  cur_vma->sbrk = new_end;
  /* The obtained vm area (only)
   * now will be alloc real ram region */

 if (vm_map_ram(caller, old_end, new_end, old_end, incnumpage, newrg) < 0) {
                return -1; /* Map the memory to MEMRAM */
  }
  
newrg->rg_start = old_end;
newrg->rg_end = new_end;

enlist_vm_freerg_list(caller->mm, newrg);  

  return 0;
}

// #endif
