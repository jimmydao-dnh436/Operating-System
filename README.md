# Phần 2.2.4 — Cơ chế dịch địa chỉ dựa trên Phân trang (Paging)

> **Môn học:** CO2018 — Hệ điều hành  
> **Phần phụ trách:** Mục 2.2.4 — Paging-based address translation mechanism  
> **Các file cần sửa:** `mm64.c`, `mm-vm.c`, `libmem.c`, `sys_mem.c`

---

## 1. Tổng quan nhiệm vụ

Mục 2.2.4 yêu cầu cài đặt toàn bộ cơ chế **dịch địa chỉ ảo sang địa chỉ vật lý** dựa trên
phân trang 5 cấp (64-bit), bao gồm:

| # | Nhiệm vụ | File | Hàm |
|---|---|---|---|
| 1 | Khởi tạo cấu trúc MM | `mm64.c` | `init_mm` |
| 2 | Đọc/ghi bảng trang PTE | `mm64.c` | `pte_get_entry`, `pte_set_fpn`, `pte_set_swap` |
| 3 | Cấp phát frame + map vào PTE | `mm64.c` | `alloc_pages_range`, `vmap_page_range` |
| 4 | Mở rộng không gian heap ảo | `mm-vm.c` | `inc_vma_limit` |
| 5 | Sửa syscall dual-space | `sys_mem.c` | `__sys_memmap` |
| 6 | Đọc/ghi dữ liệu vật lý | `libmem.c` | `pg_getpage`, `pg_getval`, `pg_setval` |
| 7 | Debug bảng trang | `mm64.c` | `print_pgtbl` |

---

## 2. Kiến trúc quan hệ các struct (Hình 8)

Đây là sơ đồ quan hệ giữa các cấu trúc dữ liệu — hiểu rõ sơ đồ này
là điều kiện tiên quyết trước khi bắt đầu code:

```
┌─────────────────────────────────────────────────────────────────┐
│  krnl_t  ←── "OS" trong Hình 8, cầu nối user ↔ hardware         │
│                                                                 │
│  +queue_t*       ready_queue    (lập lịch)                      │
│  +queue_t*       running_list   (đang chạy — dùng trong Task 5) │
│  +mm_struct*     mm       ──────────────────────────────────┐   │
│  +memphy_struct* mram     ──── RAM vật lý ──────────────┐   │   │
│  +memphy_struct* mswp[]   ──── SWAP devices ──────────┐ │   │   │
└─────────────────────────────────────────────────────── │ │ ──┘  │
                                                         │ │   │
         ┌───────────────────────────────────────────────┘ │   │
         ▼                                                  │   │
   memphy_struct                                            │   │
   +BYTE*          storage      (mảng byte vật lý)         │   │
   +framephy_struct* free_fp_list  ◄── danh sách frame trống    │
                                                            │   │
         ┌──────────────────────────────────────────────────┘   │
         ▼                                                       │
   memphy_struct (SWAP)                                          │
   +BYTE*          storage      (lưu trang bị swap ra)          │
   +framephy_struct* free_fp_list                                │
                                                                 │
         ┌───────────────────────────────────────────────────────┘
         ▼
   mm_struct  ◄── không gian địa chỉ ảo của MỘT tiến trình
   +addr_t*   pgd / p4d / pud / pmd / pt  ◄── 5 mảng bảng trang (Task 1+2)
   +vm_area_struct*  mmap  ──────────────────────────────────────┐
   +vm_rg_struct     symrgtbl[30]  ◄── bảng biến (rgid → vùng)  │
   +pgn_t*           fifo_pgn     ◄── FIFO list cho replacement  │
                                                                  │
         ┌────────────────────────────────────────────────────────┘
         ▼
   vm_area_struct  ◄── một đoạn không gian ảo liên tục
   +addr_t  vm_start / vm_end
   +addr_t  sbrk        ◄── đỉnh heap đã map vào RAM thật
   +vm_rg_struct* vm_freerg_list  ◄── danh sách vùng trống trong VMA
   +vm_area_struct* vm_next       ◄── VMA tiếp theo (bài chỉ có 1)
         │
         ▼
   vm_rg_struct  ◄── một vùng bộ nhớ liên tục (= 1 "biến")
   +addr_t rg_start / rg_end
   +vm_rg_struct* rg_next


pcb_t  ◄── Process Control Block
+krnl_t* krnl   ◄── trỏ vào krnl_t chung của toàn hệ thống
+addr_t  regs[10]
+uint32_t pc / pid / priority
```

**Ba quan hệ quan trọng nhất cần nhớ:**

```
1. pcb_t → krnl_t → mm_struct    : không gian địa chỉ ảo riêng của mỗi tiến trình
2. krnl_t → memphy_struct (mram)  : phần cứng RAM dùng chung giữa tất cả tiến trình
3. mm_struct.pt[pgn] = PTE        : ánh xạ trang ảo → frame vật lý (cốt lõi của paging)
```

---

## 3. Cấu trúc địa chỉ 64-bit và PTE

### 3.1 Phân tích địa chỉ ảo (mm64.h)

```
Bit:  63..57   56..48   47..39   38..30   29..21   20..12   11..0
       unused    PGD      P4D      PUD      PMD       PT     OFFSET
       (7 bit)  (9 bit)  (9 bit)  (9 bit)  (9 bit)  (9 bit) (12 bit)
                                                     └──────────────┘
                                                     PAGING64_ADDR_PT_SHIFT = 12
```

**Trong bài này** (`PAGING64_MAX_PGN = 512`):
- `pgn` tối đa = 511 → chỉ 9 bit → chỉ `PT index` thay đổi
- `pgd = p4d = pud = pmd = 0` với mọi `pgn`
- **Kết luận:** `pt_index == pgn` — chỉ cần dùng `mm->pt[pgn]`

### 3.2 Cấu trúc PTE (32 bit)

```
 31       30        29     28    27..15  14..13  12..0
┌──────┬─────────┬────────┬─────┬───────┬───────┬──────┐
│PRSNT │SWAPPED  │RESERVE │DIRTY│USRNUM │EMPTY  │FPN   │
└──────┴─────────┴────────┴─────┴───────┴───────┴──────┘

Khi trang ở RAM (PRESENT=1, SWAPPED=0):
  Bit 12..0  = FPN (Frame Physical Number)

Khi trang ở SWAP (PRESENT=1, SWAPPED=1):
  Bit 4..0   = SWPTYP (loại thiết bị SWAP)
  Bit 25..5  = SWPOFF (offset trong SWAP)
```

---

## 4. Hướng dẫn chi tiết từng file

---

### File 1: `mm64.c` — Lõi hệ thống phân trang

Đây là file quan trọng nhất, chứa tất cả thao tác bảng trang 5 cấp.

---

#### Task 1 — `init_mm(mm, caller)`

**Ý nghĩa:** Khởi tạo toàn bộ cấu trúc quản lý bộ nhớ cho một tiến trình mới.
Được gọi trong `os.c:ld_routine()` ngay khi load process — trước bất kỳ lệnh nào.

**Tại sao phải làm đầu tiên:**
Nếu `mm->mmap = NULL` hoặc `mm->pt = NULL`, mọi hàm sau (`get_vma_by_num`,
`pte_get_entry`...) sẽ crash ngay lập tức với segfault.

**Điều cần làm:**

```c
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* [1] Cấp phát 5 mảng bảng trang
   * Dùng calloc (không phải malloc) để khởi tạo về 0.
   * pte = 0 → bit PRESENT = 0 → "trang chưa được map" — đúng với trang mới.
   */
  mm->pgd = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->p4d = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->pud = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->pmd = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->pt  = calloc(PAGING64_MAX_PGN, sizeof(addr_t));

  /* [2] Khởi tạo VMA0 — vùng địa chỉ ảo đầu tiên
   * vm_start = vm_end = sbrk = 0: chưa có byte ảo nào tồn tại
   * vm_mm = mm: trỏ ngược để get_vma_by_num có thể duyệt về mm
   * vm_next = NULL: bài chỉ có 1 VMA
   */
  vma0->vm_id    = 0;
  vma0->vm_start = 0;
  vma0->vm_end   = 0;
  vma0->sbrk     = 0;
  vma0->vm_mm    = mm;
  vma0->vm_next  = NULL;

  struct vm_rg_struct *first_rg = init_vm_rg(0, 0);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* [3] Gán vma0 vào mm
   * mm->mmap là điểm vào — get_vma_by_num bắt đầu duyệt từ đây
   */
  mm->mmap = vma0;

  /* [4] Xóa symrgtbl, fifo_pgn, kcpooltbl
   * symrgtbl[i] = {0,0}: vùng nhớ thứ i chưa được cấp phát
   *   (hàm __free kiểm tra rg_start==0 && rg_end==0 để phát hiện double-free)
   * fifo_pgn = NULL: chưa có trang nào trong RAM, FIFO list trống
   */
  memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));
  mm->fifo_pgn  = NULL;
  mm->kcpooltbl = NULL;

  return 0;
}
```

---

#### Task 2 — `pte_get_entry()`, `pte_set_fpn()`, `pte_set_swap()`

**Ý nghĩa:** Ba hàm này là **giao diện đọc/ghi bảng trang** — bất kỳ thao tác nào
liên quan đến ánh xạ trang ảo ↔ frame vật lý đều phải đi qua một trong ba hàm này.

**Tại sao làm ngay sau Task 1:**
- `pte_get_entry` hiện tại luôn `return 0` → `pg_getpage` luôn thấy `PRESENT=0`
  → luôn cố swap → crash ngay khi không có gì để swap.
- `vmap_page_range` (Task 3) gọi `pte_set_fpn` → Task 3 vô nghĩa nếu Task 2 chưa xong.

**Pattern chung cho cả 3 hàm:**

```
Bước 1: gọi get_pd_from_pagenum(pgn, ...) → lấy pt_index
         (trong bài này pt_index == pgn luôn luôn)
Bước 2: trỏ pte vào đúng ô mm->pt[pt_index]
Bước 3: SETBIT / CLRBIT / SETVAL theo loại thao tác
```

**`pte_get_entry` — đọc PTE:**

```c
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;

  /* Tính 5 index — trong bài chỉ pt thay đổi, pt == pgn */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  /* Đọc PTE thật từ mm->pt[pt] — không return 0 cứng */
  return (uint32_t)krnl->mm->pt[pt];
}
```

**`pte_set_fpn` — ghi FPN (trang đang ở RAM):**

```c
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;

  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  /* Xóa malloc dummy, trỏ vào bảng trang thật */
  addr_t *pte = &krnl->mm->pt[pt];

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);   /* bit 31 = 1: trang có mặt */
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);  /* bit 30 = 0: không phải swap */
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}
```

**`pte_set_swap` — ghi thông tin swap (trang đã bị swap ra):**

```c
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;

  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  addr_t *pte = &krnl->mm->pt[pt];

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);   /* vẫn "tồn tại" trong không gian ảo */
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);  /* nhưng đang ở SWAP, không phải RAM */
  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}
```

---

#### Task 3 — `alloc_pages_range()` và `vmap_page_range()`

**Ý nghĩa:**
- `alloc_pages_range`: lấy `n` frame trống từ RAM (từ `free_fp_list` của `mram`)
- `vmap_page_range`: nhận danh sách frame đó, ghi PTE cho từng trang, đưa trang vào FIFO tracking

**Tại sao làm cùng nhau:** `vm_map_ram` gọi `alloc_pages_range` trước,
rồi truyền kết quả vào `vmap_page_range`. Thiếu một trong hai thì không thể map bộ nhớ.

**`alloc_pages_range`:**

```c
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum,
                          struct framephy_struct **frm_lst)
{
  addr_t fpn;
  struct framephy_struct *newfp_str;

  for (int pgit = 0; pgit < req_pgnum; pgit++)
  {
    /* Lấy 1 frame trống từ danh sách free_fp_list của RAM
     * MEMPHY_get_freefp: lấy fpn từ đầu danh sách, xóa node đó
     */
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0)
      return -3000;  /* RAM đầy — mã lỗi riêng để vm_map_ram xử lý */

    /* Tạo node framephy_struct lưu fpn vừa lấy được */
    newfp_str          = malloc(sizeof(struct framephy_struct));
    newfp_str->fpn     = fpn;
    newfp_str->owner   = caller->krnl->mm;
    newfp_str->fp_next = *frm_lst;  /* prepend vào đầu danh sách */
    *frm_lst           = newfp_str;
  }
  return 0;
}
```

**`vmap_page_range`:**

```c
addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
                        struct framephy_struct *frames,
                        struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *fpit = frames;
  addr_t pgn_start = addr / PAGING_PAGESZ;

  /* Cập nhật ret_rg — inc_vma_limit cần biết vùng nào vừa được map */
  ret_rg->rg_start = addr;
  ret_rg->rg_end   = addr + (addr_t)pgnum * PAGING_PAGESZ;
  ret_rg->vmaid    = 0;

  for (int pgit = 0; pgit < pgnum && fpit != NULL; pgit++)
  {
    addr_t pgn = pgn_start + pgit;

    /* Ghi PTE: pgn → fpit->fpn (khai sinh liên kết trang ảo ↔ frame vật lý) */
    pte_set_fpn(caller, pgn, fpit->fpn);

    /* Đưa pgn vào FIFO tracking để page replacement biết thứ tự thời gian */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);

    fpit = fpit->fp_next;
  }
  return 0;
}
```

**Sau khi cài xong, bỏ comment trong `vm_map_ram`:**

```c
/* Dòng này đang bị comment — bỏ comment sau khi alloc_pages_range xong */
ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);
```

---

#### Task 7 — `print_pgtbl(caller, start, end)`

**Ý nghĩa:** In trạng thái bảng trang sau mỗi ALLOC/FREE/READ/WRITE (khi bật `PAGETBL_DUMP`).
Dùng để so sánh output với mẫu trong thư mục `output/`.

```c
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  struct krnl_t *krnl = caller->krnl;

  addr_t pgn_start = (start == 0) ? 0 : start / PAGING_PAGESZ;
  addr_t pgn_end   = (end == (addr_t)-1) ? PAGING64_MAX_PGN
                                           : end / PAGING_PAGESZ;

  addr_t pgn;
  for (pgn = pgn_start; pgn < pgn_end; pgn++)
  {
    addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;
    get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
    addr_t pte_val = krnl->mm->pt[pt];

    /* Chỉ in trang đã được map (PRESENT=1) */
    if (PAGING_PAGE_PRESENT(pte_val)) {
      if (pte_val & PAGING_PTE_SWAPPED_MASK)
        printf("Page %lu [SWAPPED] swpoff=%lu\n",
               pgn, PAGING_SWP(pte_val));
      else
        printf("Page %lu [PRESENT] fpn=%lu\n",
               pgn, PAGING_FPN(pte_val));
    }
  }
  return 0;
}
```

---

### File 2: `mm-vm.c` — Quản lý không gian địa chỉ ảo

---

#### Task 4 — `inc_vma_limit(caller, vmaid, inc_sz)`

**Ý nghĩa:** Mở rộng heap ảo khi `__alloc` không còn vùng trống trong `vm_freerg_list`.
Hàm này phải làm 2 việc: (1) nâng `sbrk` lên, (2) map thêm frame RAM vào vùng mới mở rộng.

**Tại sao phải map RAM thật:**
Chỉ tăng `sbrk` thôi là chưa đủ — nếu không gọi `vm_map_ram`, vùng ảo mới
không có frame tương ứng trong bảng trang → mọi READ/WRITE sau đó sẽ fail.

**Tại sao làm sau Task 3:**
`inc_vma_limit` gọi `vm_map_ram` → `alloc_pages_range` + `vmap_page_range`.
Nếu Task 3 chưa xong, Task 4 hoàn toàn không có ý nghĩa.

```c
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  /* [1] Làm tròn kích thước lên bội số của page size
   * Bộ nhớ vật lý phân bổ theo đơn vị trang — không thể cấp "nửa trang"
   * Ví dụ: inc_sz=300B, PAGING64_PAGESZ=4096 → inc_amt=4096, incnumpage=1
   */
  addr_t inc_amt  = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int incnumpage  = (int)(inc_amt / PAGING64_PAGESZ);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  addr_t old_sbrk = cur_vma->sbrk;
  addr_t old_end  = cur_vma->vm_end;

  /* [2] Nâng giới hạn vùng ảo */
  cur_vma->vm_end += inc_amt;
  cur_vma->sbrk   += inc_amt;

  /* [3] Map vùng ảo mới vào frame RAM thật
   * Nếu fail (hết RAM): rollback sbrk và vm_end về vị trí cũ
   */
  struct vm_rg_struct newrg;
  if (vm_map_ram(caller, old_sbrk, cur_vma->sbrk,
                 old_end, incnumpage, &newrg) < 0)
  {
    cur_vma->vm_end = old_end;
    cur_vma->sbrk   = old_sbrk;
    return -1;
  }
  return 0;
}
```

---

### File 3: `sys_mem.c` — System call xử lý bộ nhớ

---

#### Task 5 — Sửa `__sys_memmap(krnl, pid, regs)`

**Ý nghĩa:** System call số 17 — cửa ngõ giữa userspace và kernelspace cho mọi
thao tác bộ nhớ: mở rộng VMA, swap, đọc/ghi RAM vật lý.

**Vấn đề hiện tại:**
```c
/* CODE CŨ — SAI */
struct pcb_t *caller = malloc(sizeof(struct pcb_t));  // PCB giả, rỗng!
caller->krnl = malloc(sizeof(struct krnl_t));          // krnl giả, rỗng!
/* → caller->krnl->mm = NULL → crash khi inc_vma_limit cố gọi get_vma_by_num */
```

**Tại sao không được truyền PCB trực tiếp (nguyên tắc dual-space):**
Syscall là ranh giới giữa userspace và kernelspace. Chỉ được truyền
các giá trị đơn giản (như `pid`), không được truyền con trỏ vùng nhớ
từ userspace vào kernel. Kernel phải tự tìm PCB thật từ `running_list`.

**Cách sửa:**
```c
int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
  int memop = regs->a1;
  BYTE value;

  /* [1] Tìm PCB thật từ running_list bằng pid
   * Tiến trình gọi syscall đang chạy → nó phải có trong running_list
   */
  struct pcb_t *caller = NULL;
  struct queue_t *rlist = krnl->running_list;

  int i;
  for (i = 0; i < rlist->size; i++) {
    if (rlist->proc[i] != NULL && rlist->proc[i]->pid == pid) {
      caller = rlist->proc[i];
      break;
    }
  }
  if (caller == NULL) return -1;  /* tiến trình không tìm thấy */

  /* [2] Thực hiện thao tác bộ nhớ với caller thật */
  switch (memop) {
  case SYSMEM_MAP_OP:
    vmap_pgd_memset(caller, regs->a2, regs->a3);
    break;
  case SYSMEM_INC_OP:
    /* a2 = vmaid, a3 = inc_sz (xem __alloc trong libmem.c) */
    inc_vma_limit(caller, regs->a2, regs->a3);
    break;
  case SYSMEM_SWP_OP:
    /* a2 = vicfpn (frame victim trong RAM), a3 = swpfpn (frame trong SWAP) */
    __mm_swap_page(caller, regs->a2, regs->a3);
    break;
  case SYSMEM_IO_READ:
    /* a2 = địa chỉ vật lý, kết quả trả về qua a3 */
    MEMPHY_read(caller->krnl->mram, regs->a2, &value);
    regs->a3 = value;
    break;
  case SYSMEM_IO_WRITE:
    /* a2 = địa chỉ vật lý, a3 = giá trị cần ghi */
    MEMPHY_write(caller->krnl->mram, regs->a2, regs->a3);
    break;
  default:
    printf("Unknown memop code: %d\n", memop);
    break;
  }
  return 0;
}
```

---

### File 4: `libmem.c` — Giao diện đọc/ghi bộ nhớ (userspace)

---

#### Task 6a — `pg_getpage(mm, pgn, fpn, caller)`

**Ý nghĩa:** Đảm bảo trang `pgn` đang có mặt trong RAM.
Nếu trang đang ở SWAP, thực hiện swap-in (và swap-out một trang khác để nhường frame).

**Tại sao làm sau cùng trong nhóm core:**
`pg_getpage` gọi `pte_get_entry` (Task 2), `pte_set_swap` (Task 2),
`pte_set_fpn` (Task 2), và `__sys_memmap` (Task 5) qua syscall.
Tất cả phải xong trước.

**Logic swap in/out:**

```
Khi trang pgn KHÔNG có trong RAM (PRESENT=0):

  1. find_victim_page(fifo_pgn) → vicpgn  (trang cũ nhất, sẽ bị đuổi)
  2. MEMPHY_get_freefp(mswp) → swpfpn    (vị trí trống trong SWAP)
  3. vicfpn = PAGING_FPN(pte của vicpgn)  (frame của victim trong RAM)
  4. SYSCALL SWP_OP(vicfpn, swpfpn)       (copy RAM[vicfpn] → SWAP[swpfpn])
  5. pte_set_swap(vicpgn, swpfpn)         (đánh dấu victim đã bị swap)
  6. pte_set_fpn(pgn, vicfpn)             (map frame vừa giải phóng vào pgn)
  7. enlist_pgn_node(fifo_pgn, pgn)       (đưa pgn vào FIFO tracking)
```

```c
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  {
    addr_t vicpgn, swpfpn, vicfpn;

    /* Tìm trang cũ nhất (cuối FIFO list) để swap ra */
    if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
      return -1;

    /* Lấy vị trí trống trong SWAP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
      return -1;

    /* Lấy frame của victim từ PTE */
    uint32_t vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_FPN(vicpte);

    /* Copy nội dung victim từ RAM → SWAP qua SYSCALL SWP_OP */
    struct sc_regs sregs;
    sregs.a1 = SYSMEM_SWP_OP;
    sregs.a2 = vicfpn;
    sregs.a3 = swpfpn;
    _syscall(caller->krnl, caller->pid, 17, &sregs);

    /* Cập nhật PTE của victim: đánh dấu đã bị swap */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* Map frame vừa giải phóng vào trang pgn cần truy cập */
    pte_set_fpn(caller, pgn, vicfpn);

    /* Thêm pgn vào FIFO để theo dõi cho replacement sau này */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));
  return 0;
}
```

---

#### Task 6b — `pg_getval(mm, addr, data, caller)`

**Ý nghĩa:** Đọc 1 byte tại địa chỉ ảo `addr`. Dùng SYSCALL IO_READ thay vì gọi
`MEMPHY_read` trực tiếp vì đây là hàm trong "userspace" của simulation.

```c
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Đảm bảo trang có trong RAM, lấy fpn */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

  /* Tính địa chỉ vật lý: frame_start + offset */
  int phyaddr = fpn * PAGING_PAGESZ + off;

  /* Đọc qua syscall IO_READ — kết quả trả về ở regs.a3 */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = (addr_t)phyaddr;
  regs.a3 = 0;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  *data = (BYTE)regs.a3;
  return 0;
}
```

---

#### Task 6c — `pg_setval(mm, addr, value, caller)`

**Ý nghĩa:** Ghi 1 byte tại địa chỉ ảo `addr`. Tương tự `pg_getval` nhưng dùng IO_WRITE.

```c
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

  int phyaddr = fpn * PAGING_PAGESZ + off;

  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = (addr_t)phyaddr;
  regs.a3 = (addr_t)value;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  return 0;
}
```

---

## 5. Cấu hình trước khi build (`os-cfg.h`)

```c
#define MM_PAGING           /* bật paging mode — BẮT BUỘC */
#define MM64 1              /* bật chế độ địa chỉ 64-bit — BẮT BUỘC */
//#define MM_FIXED_MEMSZ    /* comment out: file config khai báo RAM/SWAP size */

#define IODUMP 1            /* in log sau READ/WRITE */
#define PAGETBL_DUMP 1      /* in bảng trang sau mỗi thao tác (để debug) */
```

---

## 6. Build và chạy

### Build
```bash
make clean && make all
```

### Chạy từng test theo thứ tự (từ đơn giản đến phức tạp)

```bash
# Test 1: chỉ ALLOC, ít trang — kiểm tra Task 1, 2, 3
./os os_1_mlq_paging_small_1K

# Test 2: ALLOC nhiều hơn — kiểm tra inc_vma_limit (Task 4)
./os os_1_mlq_paging_small_4K

# Test 3: đầy đủ nhiều tiến trình — kiểm tra page replacement (Task 6)
./os os_1_mlq_paging

# Test 4: syscall — kiểm tra sys_memmap + dual-space (Task 5)
./os os_syscall

# Chạy tất cả và lưu output
bash run.sh
```

### So sánh với output mẫu
```bash
diff output/os_1_mlq_paging_small_1K.output <(./os os_1_mlq_paging_small_1K 2>&1)
```

> **Lưu ý:** Vì hệ thống chạy đa luồng, thứ tự dòng in có thể khác output mẫu.
> Điều quan trọng là các giá trị **PTE, FPN, địa chỉ phải đúng**,
> không phải thứ tự từng dòng.

---

## 7. Thứ tự làm và lý do

```
Task 1 (init_mm)
  └── Lý do: Nền móng — mọi hàm khác đều cần mm->mmap và mm->pt tồn tại
        ↓
Task 2 (pte_get/set)
  └── Lý do: Đọc/ghi bảng trang — Task 3 gọi pte_set_fpn, Task 6 gọi pte_get_entry
        ↓
Task 3 (alloc_pages + vmap_page_range)
  └── Lý do: Cấp frame thật + ghi PTE — Task 4 cần vm_map_ram hoạt động
        ↓
Task 4 (inc_vma_limit)
  └── Lý do: Mở rộng heap — được gọi khi __alloc hết chỗ, gọi Task 3
        ↓
Task 5 (sys_memmap)
  └── Lý do: Task 4 gọi qua SYSCALL INC_OP, Task 6 gọi IO_READ/WRITE/SWP_OP
        ↓
Task 6 (pg_getpage + pg_getval/setval)
  └── Lý do: Dùng tất cả Task trên — entry point của lệnh READ/WRITE từ CPU
        ↓
Task 7 (print_pgtbl)
  └── Lý do: Chỉ debug — không ảnh hưởng logic, làm cuối cùng
```

---

## 8. Các lỗi thường gặp và cách xử lý

| Triệu chứng | Nguyên nhân | Task cần kiểm tra |
|---|---|---|
| Segfault ngay khi load process | `mm->mmap = NULL` hoặc `mm->pt = NULL` | Task 1 |
| Vòng lặp vô tận trong pg_getpage | `pte_get_entry` luôn return 0 | Task 2 |
| ALLOC xong nhưng READ crash | `alloc_pages_range` chưa làm, frame không được map | Task 3 |
| `inc_vma_limit` không mở rộng | Quên gọi `vm_map_ram` hoặc quên bỏ comment | Task 3, 4 |
| Syscall crash "NULL mm" | `sys_memmap` vẫn dùng PCB giả | Task 5 |
| `find_victim_page` crash | `fifo_pgn = NULL` (quên init trong Task 1) | Task 1 |
| Segfault trong find_victim_page | List 1 phần tử: `prev == NULL`, duyệt đến `NULL->pg_next` | Task 6 |
| Giá trị đọc ra luôn = 0 | `pg_getval` chưa gọi SYSCALL IO_READ | Task 6b |

---

## 9. Checklist hoàn thành

```
[ ] Task 1: init_mm
    [ ] calloc 5 mảng pgd/p4d/pud/pmd/pt
    [ ] vma0->vm_mm = mm, vma0->vm_next = NULL
    [ ] mm->mmap = vma0
    [ ] memset(symrgtbl, 0)
    [ ] mm->fifo_pgn = NULL
    [ ] mm->kcpooltbl = NULL

[ ] Task 2: pte_get/set
    [ ] pte_get_entry: return mm->pt[pt], không return 0 cứng
    [ ] pte_set_fpn: xóa malloc dummy, dùng &mm->pt[pt]
    [ ] pte_set_swap: xóa malloc dummy, dùng &mm->pt[pt]

[ ] Task 3: alloc + vmap
    [ ] alloc_pages_range: vòng lặp MEMPHY_get_freefp, tạo frm_lst
    [ ] vmap_page_range: pte_set_fpn + enlist_pgn_node cho từng trang
    [ ] vm_map_ram: bỏ comment dòng alloc_pages_range

[ ] Task 4: inc_vma_limit
    [ ] PAGING64_PAGE_ALIGNSZ để tính inc_amt
    [ ] tăng sbrk và vm_end
    [ ] gọi vm_map_ram
    [ ] rollback nếu vm_map_ram fail

[ ] Task 5: sys_memmap
    [ ] xóa malloc PCB giả
    [ ] tìm caller từ running_list bằng pid
    [ ] kiểm tra caller != NULL trước khi dùng

[ ] Task 6: pg_getpage + pg_getval/setval
    [ ] pg_getpage: swap in/out logic đầy đủ
    [ ] pg_getval: SYSCALL IO_READ, lấy kết quả từ regs.a3
    [ ] pg_setval: SYSCALL IO_WRITE

[ ] Task 7: print_pgtbl
    [ ] in các trang PRESENT
    [ ] phân biệt PRESENT vs SWAPPED
```
