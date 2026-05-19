# Phần 2.2.3 — Physical Memory (`mm-memphy.c`)

> **CO2018** · File: `src/mm-memphy.c`

---

## 1. Nhiệm vụ

| # | Hàm | Việc làm |
|---|---|---|
| 1 | `init_memphy` | Khởi tạo thiết bị: malloc → memset → format |
| 2 | `MEMPHY_format` | Chia storage thành N frame, xây `free_fp_list` |
| 3 | `MEMPHY_get_freefp` | Lấy 1 frame từ free list → chuyển sang used list |
| 4 | `MEMPHY_put_freefp` | Trả frame: unlink từ used list → prepend vào free list |
| 5 | `MEMPHY_read/write` | Đọc/ghi byte — phân nhánh theo `rdmflg` |
| 6 | `MEMPHY_mv_csr` | Di cursor tuần tự (chỉ khi `rdmflg = 0`) |
| 7 | `MEMPHY_dump` | In trạng thái frame + hex dump storage |

---

## 2. Cấu trúc dữ liệu

```
memphy_struct
┌────────────────────────────────────────────────┐
│  BYTE *storage      ←── mảng byte thô          │
│  int   maxsz        ←── tổng dung lượng        │
│  int   rdmflg       ←── 1=random | 0=serial    │
│  int   cursor       ←── chỉ dùng khi rdmflg=0  │
│                                                │
│  framephy_struct *free_fp_list  ─────────────┐ │
│  framephy_struct *used_fp_list  ──────────┐  │ │
└───────────────────────────────────────────│──│─┘
                                            │  │
          ┌─────────────────────────────────┘  │
          │         ┌──────────────────────────┘
          ▼         ▼
    framephy_struct (node của linked list)
    ┌─────────────────────┐   ┌─────────────────────┐
    │  fpn  = 0           │   │  fpn  = 1           │
    │  fp_next ───────────┼──►│  fp_next ───────────┼──► NULL
    └─────────────────────┘   └─────────────────────┘


Ánh xạ frame → byte trong storage:

  storage: [ Frame 0        ][ Frame 1        ] ... [ Frame N-1      ]
           ↑                 ↑                      ↑
     fpn=0 * PAGESZ    fpn=1 * PAGESZ         fpn=(N-1) * PAGESZ

  Ví dụ: maxsz=1536, PAGESZ=256 → N = 6 frame
```

---

## 3. Cơ chế `init_memphy`

```
init_memphy(mp, max_size=1536, randomflg=1)
                │
                ▼
   ┌────────────────────────┐
   │ malloc(1536)           │  → mp->storage trỏ vào heap
   │ mp->maxsz = 1536       │
   └────────────┬───────────┘
                │
                ▼
   ┌────────────────────────┐
   │ memset(storage, 0)     │  → xóa dữ liệu rác, [??] → [00]
   └────────────┬───────────┘
                │
                ▼
   ┌────────────────────────┐    free_fp_list:
   │ MEMPHY_format(256)     │    [0]→[1]→[2]→[3]→[4]→[5]→NULL
   │ used_fp_list = NULL    │    used_fp_list: NULL
   └────────────┬───────────┘
                │
                ▼
   ┌────────────────────────┐
   │ rdmflg = 1             │  → random access
   │ (cursor = 0 nếu rdm=0) │  → serial: cần cursor
   └────────────────────────┘
```

---

## 4. Cơ chế cấp phát / thu hồi frame

### `MEMPHY_get_freefp` — lấy frame

```
TRƯỚC                              SAU get_freefp(*retfpn = 0)

free_fp_list                       free_fp_list
  │                                  │
  ▼                                  ▼
[fpn=0]──►[fpn=1]──►[fpn=2]──►NULL [fpn=1]──►[fpn=2]──►NULL

used_fp_list                       used_fp_list
  │                                  │
  ▼                                  ▼
 NULL                              [fpn=0]──►NULL

     node KHÔNG bị free() — được prepend sang used list (O(1))
```

### `MEMPHY_put_freefp` — trả frame

```
TRƯỚC                              SAU put_freefp(fpn=0)

used_fp_list                       used_fp_list
  │                                  │
  ▼                                  ▼
[fpn=1]──►[fpn=0]──►NULL           [fpn=1]──►NULL
                ▲
            tìm & unlink fpn=0
                │
                ▼
free_fp_list                       free_fp_list
  │                                  │
  ▼                                  ▼
[fpn=2]──►NULL                    [fpn=0]──►[fpn=2]──►NULL

                           prepend O(1) vào free list
```

---

## 5. Cơ chế đọc / ghi

```
MEMPHY_read(mp, addr, &value)
MEMPHY_write(mp, addr, data)
                │
                ▼
         mp->rdmflg ?
         /           \
        1              0
        │              │
        ▼              ▼
  Direct access    MEMPHY_seq_r/w
  storage[addr]        │
                       ▼
                  MEMPHY_mv_csr(addr)
                  cursor: 0 ──► 1 ──► 2 ──► ... ──► addr
                       │
                       ▼
                  storage[addr]
```

---

## 6. `MEMPHY_dump` — cấu trúc output

```
=========== MEMPHY DUMP ===========
  Capacity : 1536 bytes  (6 frames × 256 B/frame)
  Access   : random

  Free frames : 2 3 4 5  [4 total]
  Used frames : 1 0       [2 total]

  --- frame storage (hex | ASCII) ---
  frame    0 [0x000000] : 00 41 00 00 00 00 00 00 ... | .A......
  frame    1 [0x000100] : 00 00 00 00 00 00 00 00 ... | ........
  ...
====================================
```

---


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
