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
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  
  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1; 

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */ 
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  // Use caller->mm, not caller->krnl->mm
  // struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    // Update symrgtbl in caller->mm
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* Attempt to increase limit to get space */
#ifdef MM64
  addr_t inc_sz = (size + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ * PAGING64_PAGESZ;
#else
  addr_t inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif

  /* 
   * Before syscall, check if we need to swap out pages.
   * If RAM is full, we must trigger finding victim page and swapping.
   */
  // Note: This check logic depends on MEMPHY_get_freefp returning -1 if full.
  // The actual swapping mechanism is already implemented in pg_getpage 
  // via the syscall path, so we should ensure the syscall triggers it.
  
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = inc_sz;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
  caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

  *alloc_addr = rgnode.rg_start;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || caller == NULL || caller->krnl == NULL) {
        return -1;
    }

  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }

  proc->regs[reg_index] = addr;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("%s:%d\n", __func__, __LINE__);
  //In trace: in tên hàm và dòng code đang thực hiện
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  addr_t tgtfpn;
  uint32_t pte = pte_get_entry(caller, pgn);

  if (caller == NULL || mm == NULL || fpn == NULL)
    return -1;

  if (PAGING_PAGE_PRESENT(pte))
  {
    *fpn = PAGING_FPN(pte);
    return 0;
  }

  /* If the page is neither present nor marked as swapped, the mapping is invalid. */
  /*
  if ((pte & PAGING_PTE_SWAPPED_MASK) == 0)
    return -1;
  */

  /* Lazy Allocation: If the page is not present and not swapped, allocate a new frame. */
  if ((pte & PAGING_PTE_SWAPPED_MASK) == 0)
  {
    if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0)
    {
      if (pte_set_fpn(caller, pgn, tgtfpn) != 0)
        return -1;
      
      enlist_pgn_node(&mm->fifo_pgn, pgn);
      *fpn = tgtfpn;
      return 0;
    }
    // If no free frame, fall through to slow path or return error if not implemented
    return -1; 
  }

  {
    addr_t swpfpn = PAGING_SWP(pte);

    /* Fast path: RAM still has a free frame, so page-in without evicting anyone. */
    if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0)
    {
      struct sc_regs sregs;

      sregs.a1 = SYSMEM_SWP_OP;
      sregs.a2 = tgtfpn;
      sregs.a3 = swpfpn;

      if (_syscall(caller->krnl, caller->pid, 17, &sregs) != 0)
        return -1;

      MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
      if (pte_set_fpn(caller, pgn, tgtfpn) != 0)
        return -1;

      enlist_pgn_node(&mm->fifo_pgn, pgn);
      *fpn = tgtfpn;
      return 0;
    }

    /* Slow path: RAM full, so swap the requested page with a victim page. */
    {
      addr_t vicpgn;
      addr_t vicfpn;
      uint32_t vicpte;
      struct sc_regs sregs;

      if (find_victim_page(mm, &vicpgn) == -1)
        return -1;

      vicpte = pte_get_entry(caller, vicpgn);
      if (!PAGING_PAGE_PRESENT(vicpte))
        return -1;

      vicfpn = PAGING_FPN(vicpte);

      sregs.a1 = SYSMEM_SWP_OP;
      sregs.a2 = vicfpn;
      sregs.a3 = swpfpn;

      if (_syscall(caller->krnl, caller->pid, 17, &sregs) != 0)
        return -1;

      if (pte_set_swap(caller, vicpgn, 0, swpfpn) != 0)
        return -1;

      if (pte_set_fpn(caller, pgn, vicfpn) != 0)
        return -1;

      enlist_pgn_node(&mm->fifo_pgn, pgn);
      *fpn = vicfpn;
    }
  }

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;
  int phyaddr;
  struct sc_regs sregs;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  phyaddr = fpn * PAGING_PAGESZ + off;
  sregs.a1 = SYSMEM_IO_READ;
  sregs.a2 = phyaddr;
  sregs.a3 = 0;

  if (_syscall(caller->krnl, caller->pid, 17, &sregs) != 0)
    return -1;

  *data = (BYTE)sregs.a3;

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;
  int phyaddr;
  struct sc_regs sregs;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  phyaddr = fpn * PAGING_PAGESZ + off;
  sregs.a1 = SYSMEM_IO_WRITE;
  sregs.a2 = phyaddr;
  sregs.a3 = value;

  if (_syscall(caller->krnl, caller->pid, 17, &sregs) != 0)
    return -1;

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{

  if (caller == NULL || caller->krnl == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
        return -1;
    }
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);


  if (currg == NULL)
    return -1;
  
  if (currg->rg_start == 0 && currg->rg_end == 0)
    return -1; // Lỗi: Đọc từ vùng nhớ đã bị giải phóng (Use-after-free)

  if (currg->rg_start + offset >= currg->rg_end)
    return -1; // Lỗi: Tràn bộ đệm

  return pg_getval(caller->mm, currg->rg_start + offset, data, caller);

}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  if (val == 0) {
    printf("%s:%d\n", __func__, __LINE__);
  }
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{

  if (caller == NULL || caller->krnl == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
        return -1;
    }
  
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int res = pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return res;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("%s:%d\n", __func__, __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
//addr_t  addr;
//int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */

  if (caller == NULL) {
        return -1; // Lỗi: Không xác định được tiến trình gọi
    }
    if (size == 0) {
        return -1; // Lỗi: Yêu cầu cấp phát 0 byte là vô nghĩa
    }
    addr_t addr;
    int val = __kmalloc(caller, -1, reg_index, size, &addr);
    /* TO-DO: provide OS kmem allocation validation
     */
    if (val != 0) {
        return -1; // Lỗi: Cấp phát thất bại (Có thể do hết bộ nhớ Kernel)
    }

    return 0;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  //struct krnl_t *krnl = caller->krnl;
  //krnl->symrgtbl...
  //krnl->krnl_pgd ...
  struct krnl_t *krnl = caller->krnl;
    if (krnl == NULL || size == 0) {
        return -1; // Lỗi: Không hợp lệ
    }
#ifdef MM64
    int num_pages = (size + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ;
    addr_t vaddr = (addr_t)rgid * PAGING64_PAGESZ * 1000;
#else
    int num_pages = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
    addr_t vaddr = (addr_t)rgid * PAGING_PAGESZ * 1000;
#endif

    /* 3. Cấp phát RAM và Ánh xạ vào hệ thống phân trang 5 cấp */
    for (int i = 0; i < num_pages; i++) {
        addr_t fpn;

        // Lấy Frame vật lý từ RAM của Kernel
        if (MEMPHY_get_freefp(krnl->mram, &fpn) == -1) {
            return -1;
        }

        addr_t current_vaddr = vaddr + (i * PAGING64_PAGESZ);
        addr_t pgn = current_vaddr >> PAGING64_ADDR_PT_SHIFT;
        /* Sử dụng hệ thống 5 cấp đã khai báo trong krnl_t */
        if (pte_set_fpn(caller, pgn, fpn) != 0) {
            return -1; // Lỗi: Không thể ánh xạ trang ảo vào bảng trang
        }
    }

    *alloc_addr = vaddr;
    return 0;

}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management */

  //struct krnl_t *krnl = caller->krnl;
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL) {
        return -1;
    }

    if (size == 0 || align == 0 || size < align) {
        return -1; // Kích thước không hợp lệ
    }
    struct mm_struct *mm = caller->krnl->mm;
    /* 2. Cấp phát mảng Danh bạ Pool linh hoạt theo ID */
    /* Thay đổi logic cấp phát */
    if (mm->kcpooltbl == NULL) {
        // Cấp phát CỐ ĐỊNH 100 slot ngay từ lần gọi đầu tiên
        // Bất kể tiến trình gọi pool_id = 2 hay 15, mảng đều chứa được.
        mm->kcpooltbl = calloc(PAGING_MAX_KCACHE_POOLS, sizeof(struct kcache_pool_struct));

        if (mm->kcpooltbl == NULL) {
            return -1;
        }
    }
    if (cache_pool_id >= PAGING_MAX_KCACHE_POOLS) {
        return -1; // Lỗi: Vượt quá số lượng Pool tối đa của hệ thống
    }

    /* 3. Xin cấp phát RAM vật lý cho Pool */
    addr_t pool_storage_addr;
    int ret = __kmalloc(caller, -1, cache_pool_id, size, &pool_storage_addr);

    if (ret != 0) {
        return -1; // Lỗi: Quá trình ánh xạ phân trang thất bại
    }
    /* 4. Ghi chú cấu hình Pool vào kcpooltbl
     * Ép kiểu tường minh (int) để khớp với định nghĩa trong os-mm.h
     */
    mm->kcpooltbl[cache_pool_id].size = (int)size;
    mm->kcpooltbl[cache_pool_id].align = (int)align;
    mm->kcpooltbl[cache_pool_id].storage = pool_storage_addr;

    return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  if (proc == NULL || proc->krnl == NULL || proc->krnl->mm == NULL) {
        return -1; // Lỗi: Tiến trình hoặc Kernel context không hợp lệ
    }
    addr_t alloc_addr = 0; // Biến hứng địa chỉ ảo trả về
    /* 2. Chuyển tiếp yêu cầu xuống hàm helper (hàm lõi)*/
    addr_t val = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &alloc_addr);
    /* 3. Kiểm tra kết quả từ hàm helper */
    if (val != 0) {
        return -1; // Lỗi: Cấp phát Cache slot thất bại
    }
    return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL) {
        return -1;
    }

    // Kiểm tra giới hạn ID của Pool
    if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_KCACHE_POOLS) {
        return -1;
    }
    struct mm_struct *mm = caller->krnl->mm;
    
    // Kiểm tra pool có được khởi tạo chưa (kcpooltbl có thể NULL nếu chưa có pool nào)
    if (mm->kcpooltbl == NULL) {
        return -1;
    }
    
    struct kcache_pool_struct *pool = &mm->kcpooltbl[cache_pool_id];
    /* 2. Kiểm tra tình trạng của Pool */
    if (pool->align == 0 || pool->size < pool->align) {
        return -1; // Lỗi: Pool không tồn tại hoặc đã hết chỗ
    }
    /* 3. Cấp phát ô nhớ (Slot) theo cơ chế Bump Allocator */
    addr_t slot_addr = pool->storage;

    // Tịnh tiến (Bump) con trỏ kho lên ô tiếp theo
    pool->storage += pool->align;

    // Trừ đi dung lượng đã cấp phát khỏi tổng dung lượng còn lại
    pool->size -= pool->align;

    /* 4. Cập nhật Danh bạ vùng nhớ (Symbol Table) */
    if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ) {
        mm->symrgtbl[rgid].vmaid = vmaid;
        mm->symrgtbl[rgid].rg_start = slot_addr;
        mm->symrgtbl[rgid].rg_end = slot_addr + pool->align;
    } else {
        // Trả lại bộ nhớ nếu rgid không hợp lệ (Rollback)
        pool->storage -= pool->align;
        pool->size += pool->align;
        return -1;
    }
    /* 5. Trả về kết quả */
    *alloc_addr = slot_addr;

    return 0;

}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);

  if (caller == NULL || size == 0) {
        return -1;
    }
    /*
     * TO-DO: Map kernel address range
     */
    BYTE temp_data;

    /* Vòng lặp copy từng byte từ Nguồn (Kernel) sang Đích (User) */
    for (uint32_t i = 0; i < size; i++) {

        // 1. Lấy dữ liệu từ Kernel (Ví dụ: Từ Cache Pool)
        if (__read_kernel_mem(caller, -1, source, offset + i, &temp_data) != 0) {
            return -1; // Lỗi: Lỗi vùng nhớ Kernel
        }

        // 2. Trả dữ liệu về cho biến của User
        // Hàm này an toàn vì nó đã tự gọi pg_getpage() để xử lý Swap
        if (__write_user_mem(caller, -1, destination, offset + i, temp_data) != 0) {
            return -1; // Lỗi: User không có quyền ghi hoặc lỗi trang
        }
    }

    return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);

  if (caller == NULL || size == 0) {
        return -1;
    }
    /*
     * TO-DO: Map kernel address range
     */
    BYTE temp_data;

    /* Vòng lặp copy từng byte từ Nguồn (Kernel) sang Đích (User) */
    for (uint32_t i = 0; i < size; i++) {

        // 1. Lấy dữ liệu từ Kernel (Ví dụ: Từ Cache Pool)
        if (__read_kernel_mem(caller, -1, source, offset + i, &temp_data) != 0) {
            return -1; // Lỗi: Lỗi vùng nhớ Kernel
        }

        // 2. Trả dữ liệu về cho biến của User
        // Hàm này an toàn vì nó đã tự gọi pg_getpage() để xử lý Swap
        if (__write_user_mem(caller, -1, destination, offset + i, temp_data) != 0) {
            return -1; // Lỗi: User không có quyền ghi hoặc lỗi trang
        }
    }

    return 0; // Copy thành công
}


/* Helper to validate canonical address range and permissions */
static int check_memory_access(struct pcb_t *caller, addr_t vaddr, uint32_t pte, int is_kernel_access)
{
    if ((vaddr > USER_SPACE_END && vaddr < KERNEL_SPACE_START)) {
        printf("Memory Access Violation: Non-canonical address 0x%016lx\n", (unsigned long)vaddr);
        return -1;
    }

    if (!is_kernel_access && !PAGING_PAGE_IS_USER(pte)) {
        printf("Memory Access Violation: User process accessing Kernel page 0x%016lx\n", (unsigned long)vaddr);
        return -1;
    }
    
    return 0;
}

/*__read_kernel_mem - read value in kernel region memory */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
    if (caller == NULL || caller->krnl == NULL || data == NULL) return -1;
    if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) return -1;

    struct mm_struct *mm = caller->krnl->mm;
    struct vm_rg_struct *currg = &mm->symrgtbl[rgid];
    if (currg->rg_start == 0 && currg->rg_end == 0) return -1;
    if (currg->rg_start + offset >= currg->rg_end) return -1;

    addr_t vaddr = currg->rg_start + offset;
    addr_t pgn = vaddr >> PAGING64_ADDR_PT_SHIFT;
    uint32_t pte = pte_get_entry(caller, pgn);

    if (!PAGING_PAGE_PRESENT(pte)) return -1;

    // Kernel can access everything (is_kernel_access = 1)
    if (check_memory_access(caller, vaddr, pte, 1) != 0) return -1;

    addr_t phyaddr = (PAGING_FPN(pte) << PAGING64_ADDR_PT_SHIFT) + (vaddr & ((1 << PAGING64_ADDR_PT_SHIFT) - 1));

    return MEMPHY_read(caller->krnl->mram, phyaddr, data);
}

/*__write_kernel_mem - write a kernel region memory */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
    if (caller == NULL || caller->krnl == NULL) return -1;
    if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) return -1;

    struct mm_struct *mm = caller->krnl->mm;
    struct vm_rg_struct *currg = &mm->symrgtbl[rgid];
    if (currg->rg_start == 0 && currg->rg_end == 0) return -1;
    if (currg->rg_start + offset >= currg->rg_end) return -1;

    addr_t vaddr = currg->rg_start + offset;
    addr_t pgn = vaddr >> PAGING64_ADDR_PT_SHIFT;
    uint32_t pte = pte_get_entry(caller, pgn);

    if (!PAGING_PAGE_PRESENT(pte)) return -1;

    if (check_memory_access(caller, vaddr, pte, 1) != 0) return -1;

    addr_t phyaddr = (PAGING_FPN(pte) << PAGING64_ADDR_PT_SHIFT) + (vaddr & ((1 << PAGING64_ADDR_PT_SHIFT) - 1));

    return MEMPHY_write(caller->krnl->mram, phyaddr, value);
}

/*__read_user_mem - read value in user region memory */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
    if (caller == NULL || caller->krnl == NULL || caller->mm == NULL) return -1;
    struct vm_rg_struct *currg = &caller->mm->symrgtbl[rgid];
    if (currg->rg_start == 0 && currg->rg_end == 0) return -1;
    if (currg->rg_start + offset >= currg->rg_end) return -1;

    addr_t vaddr = currg->rg_start + offset;
    int pgn = vaddr >> PAGING64_ADDR_PT_SHIFT;
    int fpn;

    if (pg_getpage(caller->mm, pgn, &fpn, caller) != 0) return -1;

    uint32_t pte = pte_get_entry(caller, pgn);
    // User process accessing -> is_kernel_access = 0
    if (check_memory_access(caller, vaddr, pte, 0) != 0) return -1;

    addr_t phyaddr = (fpn << PAGING64_ADDR_PT_SHIFT) + (vaddr & ((1 << PAGING64_ADDR_PT_SHIFT) - 1));

    return MEMPHY_read(caller->krnl->mram, phyaddr, data);
}

/*__write_user_mem - write a user region memory */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
    if (caller == NULL || caller->krnl == NULL || caller->mm == NULL) return -1;
    struct vm_rg_struct *currg = &caller->mm->symrgtbl[rgid];
    if (currg->rg_start == 0 && currg->rg_end == 0) return -1;
    if (currg->rg_start + offset >= currg->rg_end) return -1;

    addr_t vaddr = currg->rg_start + offset;
    int pgn = vaddr >> PAGING64_ADDR_PT_SHIFT;
    int fpn;

    if (pg_getpage(caller->mm, pgn, &fpn, caller) != 0) return -1;

    uint32_t pte = pte_get_entry(caller, pgn);
    if (check_memory_access(caller, vaddr, pte, 0) != 0) return -1;

    addr_t phyaddr = (fpn << PAGING64_ADDR_PT_SHIFT) + (vaddr & ((1 << PAGING64_ADDR_PT_SHIFT) - 1));

    return MEMPHY_write(caller->krnl->mram, phyaddr, value);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = pte_get_entry(caller, pagenum);

    if (!pte) continue;  /* Entry chưa được cấp phát – bỏ qua */

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if (pte & PAGING_PTE_SWAPPED_MASK)
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;
 
  /* TODO: Implement the theoretical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;

  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  if (prev == NULL)
    mm->fifo_pgn = NULL;
  else
    prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */

  while (rgit != NULL) {
      if (size <= (rgit->rg_end - rgit->rg_start)) {
          // Tìm thấy vùng phù hợp
          newrg->rg_start = rgit->rg_start;
          newrg->rg_end = rgit->rg_start + size;

          // Cập nhật lại node curr
          if (size == (rgit->rg_end - rgit->rg_start)) {
              // Xóa hẳn node này
              struct vm_rg_struct *next = rgit->rg_next;
              if (next != NULL) {
                  rgit->rg_start = next->rg_start;
                  rgit->rg_end = next->rg_end;
                  rgit->rg_next = next->rg_next;
                  free(next);
              } else {
                  rgit->rg_start = rgit->rg_end;
                  rgit->rg_next = NULL;                  
              }
             
          } else {
              // Chỉ cắt bớt
              rgit->rg_start += size;
          }
          break;
      } else {
          // Không đủ lớn, tiếp tục tìm
          rgit = rgit->rg_next;
      }  
  }
  if (newrg->rg_start == -1) {
      return -1; // Không tìm thấy vùng phù hợp
  }

  return 0;
}
// #endif
