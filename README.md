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

