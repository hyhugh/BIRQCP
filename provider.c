#include <autoconf.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <elf/elf.h>
#include <elf/elf32.h>
#include <elf/elf64.h>
#include <sel4platsupport/platsupport.h>
#include <cpio/cpio.h>
#include <simple-default/simple-default.h>

#include <vka/kobject_t.h>
#include <utils/util.h>
#include <sel4/sel4.h>
#include <sel4utils/sel4_zf_logif.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>
#include <sel4/sel4_arch/mapping.h>


#define MAX_LOADABLE_REGIONS 20
#define MAX_COMPONENTS 20

typedef struct {
    seL4_Word start;
    seL4_Word end;
    seL4_Word vaddr_start;
} frame_reg_t;

typedef struct cap_node {
    seL4_Word cap;
    struct cap_node* next;
} cap_node;

typedef struct {
    int belongs;
} free_slot_rep;

typedef struct {
    seL4_CPtr vspace;
    seL4_CPtr cspace;
    seL4_CPtr tcb;
    seL4_Word pc;
    seL4_Word bi_vaddr;
    seL4_Word min_vaddr;
    seL4_Word max_vaddr;
    seL4_Word elf_index;
    seL4_Word free_slot_start;
    // TODO: determine which devices to pass into each component
    seL4_Word need_devices;
    seL4_Word num_regs;
    frame_reg_t regs[20];
    seL4_Word ui_vspace_start_cap;
    seL4_Word ui_vspace_end_cap;
} component_t;

static seL4_CPtr syscall_ep;
static frame_reg_t devices;

char _component_cpio[50];
char _component_cpio_end[50];
static unsigned long cpio_size ;

#define NUM_FREE_SLOTS_MAX 30000
static free_slot_rep free_slots[NUM_FREE_SLOTS_MAX];

static seL4_CPtr free_slot_start, free_slot_end;

static seL4_BootInfo *bootinfo;
#define copy_addr (ROUND_UP(((uintptr_t)_end) + (PAGE_SIZE_4K * 3), 0x1000000))
static char copy_addr_with_pt[PAGE_SIZE_4K] __attribute__((aligned(PAGE_SIZE_4K)));

#define PML4_SLOT(vaddr) ((vaddr >> (seL4_PDPTIndexBits + seL4_PageDirIndexBits + seL4_PageTableIndexBits + seL4_PageBits)) & MASK(seL4_PML4IndexBits))
#define PDPT_SLOT(vaddr) ((vaddr >> (seL4_PageDirIndexBits + seL4_PageTableIndexBits + seL4_PageBits)) & MASK(seL4_PDPTIndexBits))
#define PD_SLOT(vaddr)   ((vaddr >> (seL4_PageTableIndexBits + seL4_PageBits)) & MASK(seL4_PageDirIndexBits))
#define PT_SLOT(vaddr)   ((vaddr >> seL4_PageBits) & MASK(seL4_PageTableIndexBits))
#define PGD_SLOT(vaddr) ((vaddr >> (seL4_PUDIndexBits + seL4_PageDirIndexBits + seL4_PageTableIndexBits + seL4_PageBits)) & MASK(seL4_PGDIndexBits))
#define PUD_SLOT(vaddr) ((vaddr >> (seL4_PageDirIndexBits + seL4_PageTableIndexBits + seL4_PageBits)) & MASK(seL4_PUDIndexBits))

static seL4_CPtr untyped_cptrs[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
char __executable_start[50];
char _end[50];

static seL4_Word curr_untyped = 0;
static seL4_Word num_untyped = 0;

static component_t components[MAX_COMPONENTS];
seL4_Word curr_component = 0;

static void create_object(seL4_Word type, seL4_Word size, seL4_Word root, seL4_Word slot)
{
    int error;
    do {
        error = seL4_Untyped_Retype(untyped_cptrs[curr_untyped], type, size, root, 0, 0, slot, 1);
        if (error) {
            curr_untyped++;
        }
    } while (error && curr_untyped < num_untyped);

    if (error) {
        ZF_LOGF("out of untyped memory");
    }
}

static void
sort_untypeds(seL4_BootInfo *bootinfo)
{
    seL4_CPtr untyped_start = bootinfo->untyped.start;
    seL4_CPtr untyped_end = bootinfo->untyped.end;
    num_untyped = untyped_end - untyped_start;

    ZF_LOGD("Sorting untypeds...\n");

    seL4_Word count[CONFIG_WORD_SIZE] = {0};

    // Count how many untypeds there are of each size.
    for (seL4_Word untyped_index = 0; untyped_index != untyped_end - untyped_start; untyped_index++) {
        if (!bootinfo->untypedList[untyped_index].isDevice) {
            count[bootinfo->untypedList[untyped_index].sizeBits] += 1;
        }
    }

    // Calculate the starting index for each untyped.
    seL4_Word total = 0;
    for (seL4_Word size = CONFIG_WORD_SIZE - 1; size != 0; size--) {
        seL4_Word oldCount = count[size];
        count[size] = total;
        total += oldCount;
    }

    // Store untypeds in untyped_cptrs array.
    for (seL4_Word untyped_index = 0; untyped_index != untyped_end - untyped_start; untyped_index++) {
        if (bootinfo->untypedList[untyped_index].isDevice) {
            if (devices.start == 0) {
                devices.start = untyped_index;
                devices.end = untyped_index;
            } else {
                devices.end++;
            }
            ZF_LOGD("Untyped %3d (cptr=%u) (addr=%p) is of size %2d. Skipping as it is device\n",
                    untyped_index, (void*)(untyped_start + untyped_index),
                    (void*)bootinfo->untypedList[untyped_index].paddr,
                    bootinfo->untypedList[untyped_index].sizeBits);
        } else {
            ZF_LOGD("Untyped %3d (cptr=%u) (addr=%p) is of size %2d. Placing in slot %d...\n",
                    untyped_index, (void*)(untyped_start + untyped_index),
                    (void*)bootinfo->untypedList[untyped_index].paddr,
                    bootinfo->untypedList[untyped_index].sizeBits,
                    count[bootinfo->untypedList[untyped_index].sizeBits]);

            untyped_cptrs[count[bootinfo->untypedList[untyped_index].sizeBits]] = untyped_start +  untyped_index;
            count[bootinfo->untypedList[untyped_index].sizeBits] += 1;
        }
    }
}

static seL4_CPtr get_slot(int who)
{
    if (free_slot_start >= NUM_FREE_SLOTS_MAX) {
        free_slot_start = 5000;
    }

    while (free_slots[free_slot_start].belongs != -2) {
        free_slot_start++;
    }

    free_slots[free_slot_start].belongs = who;
    return free_slot_start++;
}

static void load_elf(const char* elf_name, component_t *component)
{
    int error;

    seL4_CPtr new_vroot = get_slot(component->elf_index);
#if defined(CONFIG_ARCH_X86_64)
    create_object(seL4_X64_PML4Object, 0, seL4_CapInitThreadCNode, new_vroot);
#else
    create_object(seL4_ARM_PageDirectoryObject, 0, seL4_CapInitThreadCNode, new_vroot);
#endif
    error = seL4_ARCH_ASIDPool_Assign(seL4_CapInitThreadASIDPool, new_vroot);
    ZF_LOGF_IFERR(error, "Failed to assign page to asid_pool");

    component->vspace = new_vroot;

    unsigned long elf_size;

    void *elf_file = cpio_get_file(_component_cpio, cpio_size, elf_name, &elf_size);
    if (!elf_file) {
        ZF_LOGF("Didn't find the elf %s", elf_name);
    }
    elf_t elf;
    error = elf_newFile(elf_file, elf_size, &elf);
    if (error < 0) {
        ZF_LOGF("elf %s is invalid", elf_name);
    }

    seL4_Word min;
    seL4_Word max;
    error = elf_getMemoryBounds(&elf, 0, &min, &max);
    ZF_LOGD("Min is %lx, Max is %lx", min, max);
    ZF_LOGF_IF(error != 1, "failed to get memory bounds");

    component->min_vaddr = min;
    component->max_vaddr = ROUND_UP(max, PAGE_SIZE_4K);
    component->bi_vaddr = ROUND_UP(max, PAGE_SIZE_4K) + PAGE_SIZE_4K;

    component->pc = elf_getEntryPoint(&elf);
    component->num_regs = 0;

    ZF_LOGD("   ELF loading %s (from %p)... \n", elf_name, elf_file);
    for (int i = 0; i < elf_getNumProgramHeaders(&elf); i++) {
        ZF_LOGD("    to %p... ", (void*)(uintptr_t)elf_getProgramHeaderVaddr(&elf, i));

        size_t f_len = elf_getProgramHeaderMemorySize(&elf, i);
        size_t f_len_file = elf_getProgramHeaderFileSize(&elf, i);
        uintptr_t dest = elf_getProgramHeaderVaddr(&elf, i);
        uintptr_t src = (uintptr_t) elf_getProgramSegment(&elf, i);

        if (elf_getProgramHeaderType(&elf, i) != PT_LOAD) {
            ZF_LOGD("Skipping non loadable header");
            continue;
        }

        // here we should create frames for loadable sections
        ZF_LOGD("f_len is 0x%x", f_len);
        ZF_LOGD("dest is %p", dest);
        ZF_LOGD("src is %p", src);

        seL4_Word num_pages = (dest + f_len)  / PAGE_SIZE_4K - (dest / PAGE_SIZE_4K) + 1;
        ZF_LOGD("# pages needed is %u", num_pages);

        if (num_pages + free_slot_start >= NUM_FREE_SLOTS_MAX) {
            free_slot_start  = 5000;
        }

        int start_page_slot = free_slot_start;
        int error;
        for (int i = 0; i < num_pages; ++i) {
            create_object(seL4_ARCH_4KPage, 0, seL4_CapInitThreadCNode, get_slot(component->elf_index));
        }
        int end_page_slot = free_slot_start;
        ZF_LOGD("For this section the first page is in slot %u the last page is in slot %u", start_page_slot, end_page_slot);

        int curr_reg = component->num_regs;
        component->regs[curr_reg].start = start_page_slot;
        component->regs[curr_reg].end = end_page_slot;
        component->regs[curr_reg].vaddr_start = dest;
        component->num_regs += 1;

        uintptr_t vaddr = dest;
        // map the frames into addr
        while (vaddr < dest + f_len) {
            ZF_LOGD(".");

            seL4_Word page = (vaddr / PAGE_SIZE_4K - dest / PAGE_SIZE_4K);
            /* seL4_Word page = (vaddr - dest) / PAGE_SIZE_4K; */
            /* map frame into the loader's address space so we can write to it */
            seL4_CPtr sel4_page = start_page_slot + page;

            ZF_LOGD("Current page :%u vaddr :0x%x target:0x%x", sel4_page, vaddr, dest + f_len);

            seL4_CPtr sel4_page_pt = 0;
            size_t sel4_page_size = PAGE_SIZE_4K;

            seL4_ARCH_VMAttributes attribs = seL4_ARCH_Default_VMAttributes;

            int error = seL4_ARCH_Page_Map(sel4_page, seL4_CapInitThreadVSpace, (seL4_Word)copy_addr, seL4_AllRights, attribs);

            if (error == seL4_FailedLookup) {
                sel4_page_pt = get_slot(component->elf_index);
                create_object(seL4_ARCH_PageTableObject, 0, seL4_CapInitThreadCNode, sel4_page_pt);
                error = seL4_ARCH_PageTable_Map(sel4_page_pt, seL4_CapInitThreadVSpace, (seL4_Word)copy_addr, seL4_ARCH_Default_VMAttributes);
                ZF_LOGF_IFERR(error, "Failed to map new pt");
                error = seL4_ARCH_Page_Map(sel4_page, seL4_CapInitThreadVSpace, (seL4_Word)copy_addr, seL4_AllRights, attribs);
            }

            if (error) {
                /* Try and retrieve some useful information to help the user
                 * diagnose the error.
                 */
                ZF_LOGD("Failed to map frame ");
                seL4_ARCH_Page_GetAddress_t addr UNUSED = seL4_ARCH_Page_GetAddress(sel4_page);
                if (addr.error) {
                    ZF_LOGD("<unknown physical address (error = %d)>", addr.error);
                } else {
                    ZF_LOGD("%p", (void*)addr.paddr);
                }
                ZF_LOGD(" -> %p (error = %d)\n", (void*)copy_addr, error);
                ZF_LOGF_IFERR(error, "");
            }

            /* copy until end of section or end of page */
            size_t len = dest + f_len - vaddr;
            if (len > sel4_page_size - (vaddr % sel4_page_size)) {
                len = sel4_page_size - (vaddr % sel4_page_size);
            }

            // only copy content with the file(leave .bss section part frames blank)
            if (vaddr < f_len_file + dest) {
                memcpy((void *) (copy_addr + vaddr % sel4_page_size), (void *) (src + vaddr - dest), len);
            }

            error = seL4_ARCH_Page_Unmap(sel4_page);
            ZF_LOGF_IFERR(error, "");

            if (sel4_page_pt != 0) {
                error = seL4_ARCH_PageTable_Unmap(sel4_page_pt);
                ZF_LOGF_IFERR(error, "");
            }

            vaddr += len;
        }


        vaddr = dest;
        while (vaddr < dest + f_len) {
            // here we map the frames into the new vspace
            size_t len = dest + f_len - vaddr;
            size_t sel4_page_size = PAGE_SIZE_4K;

            seL4_Word page = (vaddr / PAGE_SIZE_4K - dest / PAGE_SIZE_4K);
            /* map frame into the loader's address space so we can write to it */
            seL4_CPtr sel4_page = start_page_slot + page;
            ZF_LOGD("Page detail: lenleft = 0x%x vaddr = 0x%x dest = 0x%x page = %u", len, vaddr, dest, sel4_page);
            ZF_LOGD("While mapping pages into new vspace current page is %u", sel4_page);

            if (len > sel4_page_size - (vaddr % sel4_page_size)) {
                len = sel4_page_size - (vaddr % sel4_page_size);
            }

            seL4_ARCH_VMAttributes attribs = seL4_ARCH_Default_VMAttributes;

            error = seL4_ARCH_Page_Map(sel4_page, new_vroot, ROUND_DOWN(vaddr, PAGE_SIZE_4K), seL4_AllRights, attribs);
            if (error == seL4_FailedLookup) {
                seL4_Word sel4_page_pt = get_slot(component->elf_index);
                create_object(seL4_ARCH_PageTableObject, 0, seL4_CapInitThreadCNode, sel4_page_pt);
                error = seL4_ARCH_PageTable_Map(sel4_page_pt, new_vroot, vaddr, seL4_ARCH_Default_VMAttributes);

                if (error) {
#if defined(CONFIG_ARCH_X86_64)
                    seL4_CPtr new_pdpt = get_slot(component->elf_index);
                    create_object(seL4_X86_PDPTObject, 0, seL4_CapInitThreadCNode, new_pdpt);
                    error = seL4_X86_PDPT_Map(new_pdpt, new_vroot, vaddr, seL4_ARCH_Default_VMAttributes);
                    ZF_LOGF_IFERR(error, "Failed to map pdpt");

                    seL4_CPtr new_pd = get_slot(component->elf_index);
                    create_object(seL4_X86_PageDirectoryObject, 0, seL4_CapInitThreadCNode, new_pd);
                    error = seL4_X86_PageDirectory_Map(new_pd, new_vroot, vaddr, seL4_ARCH_Default_VMAttributes);
                    ZF_LOGF_IFERR(error, "Failed to map pd");

                    seL4_CPtr new_pt = get_slot(component->elf_index);
                    create_object(seL4_X86_PageTableObject, 0, seL4_CapInitThreadCNode, new_pt);
                    error = seL4_X86_PageTable_Map(new_pt, new_vroot, vaddr, seL4_ARCH_Default_VMAttributes);
                    ZF_LOGF_IFERR(error, "Failed to map pt");
#else
                    seL4_CPtr new_pdpt = get_slot(component->elf_index);
                    create_object(seL4_ARM_PageTableObject, 0, seL4_CapInitThreadCNode, new_pdpt);
                    error = seL4_ARM_PageTable_Map(new_pdpt, new_vroot, vaddr, seL4_ARCH_Default_VMAttributes);
                    ZF_LOGF_IFERR(error, "Failed to map pt");
#endif
                }
                error = seL4_ARCH_Page_Map(sel4_page, new_vroot, vaddr, seL4_AllRights, attribs);
                ZF_LOGF_IFERR(error, "Failed to map page into new vroot %u", error);
            }

            vaddr += len;
        }
    }
}

static void move_ui_frames_cap(seL4_BootInfo* bi, component_t* component)
{
    seL4_Word start = component->free_slot_start;
    ZF_LOGD("num regions is %u", component->num_regs);
    for (int i = 0; i < component->num_regs; ++i) {
        ZF_LOGD("For this section start is %u, end is %u", component->regs[i].start, component->regs[i].end);

        for (int j = component->regs[i].start; j < component->regs[i].end; ++j) {
            int error = seL4_CNode_Move(component->cspace,
                                        component->free_slot_start++,
                                        CONFIG_WORD_SIZE,

                                        seL4_CapInitThreadCNode,
                                        j,
                                        CONFIG_WORD_SIZE
                                       );
            ZF_LOGF_IFERR(error, "Failed to move ui frame caps into bi");
        }
    }
    seL4_Word end = component->free_slot_start;

    bi->userImageFrames.start = start;
    bi->userImageFrames.end = end;
    component->ui_vspace_start_cap = start;
    component->ui_vspace_end_cap = end;

    ZF_LOGD("For this ui start is %u, end is %u", start, end);
}

static void move_untyped_cap(seL4_BootInfo* bi, component_t* component)
{
    int error;
    ZF_LOGD("start creating untyped for component");
    seL4_Word start = component->free_slot_start;
#define NUM_UNTYPEDS_COMPONENT 10
    for (int i = 0; i < NUM_UNTYPEDS_COMPONENT; ++i) {
        ZF_LOGD(".");
        seL4_CPtr new_untyped = get_slot(component->elf_index);
        create_object(seL4_UntypedObject, 22, seL4_CapInitThreadCNode, new_untyped);

        error = seL4_CNode_Copy(
                    component->cspace,
                    component->free_slot_start++,
                    CONFIG_WORD_SIZE,

                    seL4_CapInitThreadCNode,
                    new_untyped,
                    CONFIG_WORD_SIZE,
                    seL4_AllRights
                );

        ZF_LOGF_IFERR(error, "Failed to move untyped");
        bi->untypedList[i].sizeBits = 22;
    }

    // put device untypeds into child components
    if (component->need_devices) {
        for (int i = devices.start; i < devices.end; i++) {
            ZF_LOGD(">");
            error = seL4_CNode_Copy(
                        component->cspace,
                        component->free_slot_start++,
                        CONFIG_WORD_SIZE,

                        seL4_CapInitThreadCNode,
                        bootinfo->untyped.start + i,
                        CONFIG_WORD_SIZE,
                        seL4_AllRights
                    );

            ZF_LOGD("device %u paddr is %lx", i, bootinfo->untypedList[i].paddr);
            ZF_LOGD("put into slot %u", component->free_slot_start - 1);
            ZF_LOGF_IFERR(error, "Failed to copy device untyped");

            int count = NUM_UNTYPEDS_COMPONENT + i - devices.start;
            bi->untypedList[count].paddr = bootinfo->untypedList[i].paddr;
            bi->untypedList[count].sizeBits = bootinfo->untypedList[i].sizeBits;
            bi->untypedList[count].isDevice = bootinfo->untypedList[i].isDevice;
        }
    }

    bi->untyped.start = start;
    bi->untyped.end = component->free_slot_start;

    bi->empty.start = component->free_slot_start + 1;
    bi->empty.end = 25000;
}

static void pass_ui_frames_paging_cap(seL4_BootInfo* bi, component_t* component)
{
    // TODO: pass userImagePaging caps
}

static int setup_component_bootinfo(component_t* component)
{
    seL4_CPtr bi_frame = get_slot(component->elf_index);
    seL4_CPtr sel4_page_pt = 0;
    int error;
    create_object(seL4_ARCH_4KPage, 0, seL4_CapInitThreadCNode, bi_frame);
    error = seL4_ARCH_Page_Map(bi_frame, seL4_CapInitThreadVSpace, (seL4_Word)copy_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    if (error = seL4_FailedLookup) {
        sel4_page_pt = get_slot(component->elf_index);
        create_object(seL4_ARCH_PageTableObject, 0, seL4_CapInitThreadCNode, sel4_page_pt);
        error = seL4_ARCH_PageTable_Map(sel4_page_pt, seL4_CapInitThreadVSpace, (seL4_Word)copy_addr, seL4_ARCH_Default_VMAttributes);
        ZF_LOGF_IFERR(error, "Failed to map new pt");

        error = seL4_ARCH_Page_Map(bi_frame, seL4_CapInitThreadVSpace, (seL4_Word)copy_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    }
    ZF_LOGF_IFERR(error, "Failed to map bi frame");

    seL4_BootInfo* bi;
    bi = (void*)copy_addr;

    bi->nodeID = bootinfo->nodeID;
    bi->numNodes = bootinfo->numNodes;
    bi->numIOPTLevels = bootinfo->numIOPTLevels;
    bi->initThreadCNodeSizeBits = bootinfo->initThreadCNodeSizeBits;
    bi->initThreadDomain = bootinfo->initThreadDomain;
    bi->extraLen = 0;
    bi->extraBIPages.start = 0;
    bi->extraBIPages.end = 0;

    move_ui_frames_cap(bi, component);
    pass_ui_frames_paging_cap(bi, component);
    move_untyped_cap(bi, component);

    seL4_Word ui_end = component->max_vaddr;
    ZF_LOGD("ui_end is %lx", ui_end);
    bi->ipcBuffer = (void*)ui_end;

    // NOTE: bi_frame is unmapped from copy_addr here
    // don't try to write to it after this line
    seL4_ARCH_Page_Unmap(bi_frame);
    if (sel4_page_pt) {
        seL4_ARCH_PageTable_Unmap(sel4_page_pt);
    }

    error = seL4_ARCH_Page_Map(bi_frame, component->vspace, ui_end + PAGE_SIZE_4K, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    ZF_LOGF_IFERR(error, "Failed to map bi_frame %u error", error);

    error = seL4_CNode_Copy(
                component->cspace,
                seL4_CapBootInfoFrame,
                CONFIG_WORD_SIZE,

                seL4_CapInitThreadCNode,
                bi_frame,
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move bi_frame cap in");

    return error;
}

static int setup_component_cnode(component_t* component)
{
    int error =  0;

    seL4_CPtr cnode = component->cspace;
    seL4_CPtr tcb = component->tcb;
    seL4_CPtr vroot = component->vspace;

    error = seL4_CNode_Copy(
                cnode,
                seL4_CapDomain,
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode,
                seL4_CapDomain,
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move domain cap into cnode");

    error = seL4_CNode_Copy(
                cnode,
                seL4_CapInitThreadVSpace,
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode,
                vroot,
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move domain cap into cnode");

    //badge syscall_eq to identify which component is calling
    error = seL4_CNode_Mint(
                cnode,
                seL4_CapIRQControl,
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode,
                syscall_ep,
                CONFIG_WORD_SIZE,
                seL4_AllRights,
                component->elf_index
            );
    ZF_LOGF_IFERR(error, "Failed to move irqcontrol cap into cnode");

    error = seL4_CNode_Copy(
                cnode,
                seL4_CapInitThreadTCB,
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode,
                tcb,
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move tcb cap into cnode");

    error = seL4_CNode_Copy(
                cnode, // to cnode
                seL4_CapInitThreadCNode, // to slot
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode, // from cnode
                cnode, // from slot
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move cnode cap into cnode");

    error = seL4_CNode_Copy(
                cnode, // to cnode
                seL4_CapInitThreadASIDPool, // to slot
                CONFIG_WORD_SIZE,

                seL4_CapInitThreadCNode, // from cnode
                seL4_CapInitThreadASIDPool, // from slot
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move cnode cap into cnode");
}

static void
initialise_component(seL4_CPtr root_cnode, seL4_CPtr root_tcb, component_t *component)
{
    int error;
    seL4_CPtr new_tcb = get_slot(component->elf_index);
    seL4_CPtr new_cnode = get_slot(component->elf_index);
    seL4_CPtr new_vspace = component->vspace;

    seL4_Word init_thread_cnode_size = bootinfo->initThreadCNodeSizeBits;

    seL4_Word cnode_size = 21;

    // create the cnode
    create_object(seL4_CapTableObject, cnode_size, root_cnode, new_cnode);

    // here we should also put all needed capabilities into the new cnode
    create_object(seL4_TCBObject, seL4_TCBBits, root_cnode, new_tcb);

    seL4_DebugNameThread(new_tcb, "capdl-loader");

    seL4_CPtr minted_new_cnode = get_slot(component->elf_index);
    error = seL4_CNode_Mint(
                seL4_CapInitThreadCNode,
                minted_new_cnode,
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode,
                new_cnode,
                CONFIG_WORD_SIZE,
                seL4_AllRights,
                seL4_CNode_CapData_new(0, CONFIG_WORD_SIZE - cnode_size).words[0]
            );

    ZF_LOGF_IFERR(error, "Failed to mint the new cnode cap");

    component->cspace = minted_new_cnode;
    component->vspace = new_vspace;
    component->tcb = new_tcb;

    setup_component_cnode(component);
}

static void setup_component_tcb(component_t *component)
{
    seL4_CPtr tcb = component->tcb;
    seL4_CPtr cspace = component->cspace;
    seL4_CPtr vspace = component->vspace;
    int error;

    error = seL4_TCB_Configure(tcb, 0, cspace, 0, vspace, 0, 0, 0);
    ZF_LOGF_IFERR(error, "Failed to configure tcb");
    error = seL4_TCB_SetPriority(tcb, seL4_CapInitThreadTCB, 255);
    ZF_LOGF_IFERR(error, "Failed to set priority");
    error = seL4_TCB_SetMCPriority(tcb, seL4_CapInitThreadTCB, 255);
    ZF_LOGF_IFERR(error, "Failed to set priority");

    seL4_UserContext regs = {0};
    error = seL4_TCB_ReadRegisters(tcb, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    ZF_LOGF_IFERR(error, "Failed to read registers");

    // here we set the ip to _sel4_start since we set the entry point to _sel4_start, since it
    // will set the stack pointer, we don't need to pass in a stack pointer in here
    sel4utils_arch_init_local_context((void*)component->pc, 0, 0, 0, 0, &regs);

#ifdef CONFIG_ARCH_ARM
    // setup different register to the bootinfo frame vaddr for different archs
    regs.r0 = component->bi_vaddr;
#endif

    error = seL4_TCB_WriteRegisters(tcb, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    ZF_LOGF_IFERR(error, "Failed to write registers");

    error = seL4_TCB_ReadRegisters(tcb, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);

    seL4_CPtr ipc_frame = get_slot(component->elf_index);
    create_object(seL4_ARCH_4KPage, 0, seL4_CapInitThreadCNode, ipc_frame);
    error = seL4_ARCH_Page_Map(ipc_frame, component->vspace, component->max_vaddr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    ZF_LOGF_IFERR(error, "Failed to map ipc frame");
    error = seL4_TCB_SetIPCBuffer(component->tcb, (seL4_Word)component->max_vaddr, ipc_frame);
    ZF_LOGF_IFERR(error, "Failed to set ipc buffer");
    error = seL4_CNode_Copy(
                component->cspace,
                seL4_CapInitThreadIPCBuffer,
                CONFIG_WORD_SIZE,
                seL4_CapInitThreadCNode,
                ipc_frame,
                CONFIG_WORD_SIZE,
                seL4_AllRights
            );
    ZF_LOGF_IFERR(error, "Failed to move ipcbuffer cap");
}

static void setup_component(int i)
{
    const char *name = NULL;
    unsigned long size;
    void *ptr = cpio_get_entry(_component_cpio, cpio_size, i, &name, &size);
    if (ptr == NULL) {
        ZF_LOGF("Wrong index passed in");
    }
    ZF_LOGD("  %d: %s, offset: %p, size: %lu\n", i, name, (void*)((uintptr_t)ptr - (uintptr_t)_component_cpio), size);

    curr_component++;
    component_t *component  = &(components[i]);
    component->free_slot_start = seL4_NumInitialCaps + 1;
    component->elf_index = i;

    if (i == 0) {
        component->need_devices = 1;
    }

    load_elf(name, component);

    initialise_component(seL4_CapInitThreadCNode, seL4_CapInitThreadTCB, component);
    setup_component_tcb(component);
    //XXX: here we put the bootinfo frame into the new tcb
    setup_component_bootinfo(component);
}

static void
setup_components()
{
    ZF_LOGD("Initialising components...\n");
    ZF_LOGD(" Available component ELFs:\n");

    for (int j = 0;; j++) {
        const char *name = NULL;
        unsigned long size;
        void *ptr = cpio_get_entry(_component_cpio, cpio_size, j, &name, &size);
        if (ptr == NULL) {
            break;
        }
        setup_component(j);
    }
}

static void resume_components()
{
    for (int i = 0; i < curr_component; ++i) {
        seL4_TCB_Resume(components[i].tcb);
    }
}


void init_copy_frame()
{
    /* An original frame will be mapped, backing copy_addr_with_pt. For
     * correctness we should unmap this before mapping into this
     * address. We locate the frame cap by looking in boot info
     * and knowing that the userImageFrames are ordered by virtual
     * address in our address space. The flush is probably not
     * required, but doesn't hurt to be cautious.
     */

    /* Find the number of frames in the user image according to
     * bootinfo, and compare that to the number of frames backing
     * the image computed by comparing start and end symbols. If
     * these numbers are different, assume the image was padded
     * to the left. */

    ZF_LOGD("in bootinfo start = %u, end = %u", bootinfo->userImageFrames.start, bootinfo->userImageFrames.end);
    ZF_LOGD("global var start = %p, end = %p", &__executable_start, &_end);
    unsigned int num_user_image_frames_reported =
        bootinfo->userImageFrames.end - bootinfo->userImageFrames.start;
    unsigned int num_user_image_frames_measured =
        (ROUND_UP((uintptr_t)&_end, PAGE_SIZE_4K) -
         (uintptr_t)&__executable_start) / PAGE_SIZE_4K;

    ZF_LOGD("reported %u frames, measured %u frames", num_user_image_frames_reported, num_user_image_frames_measured);

    if (num_user_image_frames_reported < num_user_image_frames_measured) {
        ZF_LOGD("Too few frames caps in bootinfo to back user image");
        return;
    }

    /* Here we tried to put the extra bytes before
     * the __executable_start symbol */
    size_t additional_user_image_bytes =
        (num_user_image_frames_reported - num_user_image_frames_measured) * PAGE_SIZE_4K;

    if (additional_user_image_bytes > (uintptr_t)&__executable_start) {
        ZF_LOGD("User image padding too high to fit before start symbol");
        return;
    }

    uintptr_t lowest_mapped_vaddr =
        (uintptr_t)&__executable_start - additional_user_image_bytes;
    ZF_LOGD("lowest mapped vaddr is %p", lowest_mapped_vaddr);
    ZF_LOGD("lowest mapped # frame is %u", lowest_mapped_vaddr / PAGE_SIZE_4K);

    ZF_LOGD("copy_addr_with_pt addr is %p", (uintptr_t)copy_addr_with_pt);
    ZF_LOGD("copy_addr_with_pt # frame is %u", (uintptr_t)copy_addr_with_pt / PAGE_SIZE_4K);

    seL4_CPtr copy_addr_frame = bootinfo->userImageFrames.start +
                                ((uintptr_t)copy_addr_with_pt) / PAGE_SIZE_4K -
                                lowest_mapped_vaddr / PAGE_SIZE_4K;
    /* We currently will assume that we are on a 32-bit platform
     * that has a single PD, followed by all the PTs. So to find
     * our PT in the paging objects list we just need to add 1
     * to skip the PD */

    /* bootinfo->userImagePaging.start is the cap to the PD
     * so bootinfo->userImagePaging.start + 1 is the start of PT
     * */
    seL4_CPtr copy_addr_pt = bootinfo->userImagePaging.start + 1 +
                             PD_SLOT(((uintptr_t)copy_addr)) - PD_SLOT(((uintptr_t)&__executable_start));
#if defined(CONFIG_ARCH_X86_64) || defined(CONFIG_ARCH_AARCH64)
    /* guess that there is one PDPT and PML4 on x86_64 or one PGD and PUD on aarch64 */
    copy_addr_pt += 2;
#endif

    int error;

    ZF_LOGD("size of copy_addr_with_pt is %u", sizeof(copy_addr_with_pt));
    // for each page of copy_addr_with_pt
    for (int i = 0; i < sizeof(copy_addr_with_pt) / PAGE_SIZE_4K; i++) {
#ifdef CONFIG_ARCH_ARM
        error = seL4_ARM_Page_Unify_Instruction(copy_addr_frame + i, 0, PAGE_SIZE_4K);
        ZF_LOGF_IFERR(error, "");
#endif
        error = seL4_ARCH_Page_Unmap(copy_addr_frame + i);
        ZF_LOGF_IFERR(error, "");

        if ((i + 1) % BIT(seL4_PageTableIndexBits) == 0) {
            error = seL4_ARCH_PageTable_Unmap(copy_addr_pt + i / BIT(seL4_PageTableIndexBits));
            ZF_LOGF_IFERR(error, "");
        }
    }
}

static void
parse_bootinfo(seL4_BootInfo *bootinfo)
{
    ZF_LOGD("Parsing bootinfo...\n");

    free_slot_start = bootinfo->empty.start;
    free_slot_end = bootinfo->empty.end;

    /* When using libsel4platsupport for printing support, we end up using some
     * of our free slots during serial port initialisation. Skip over these to
     * avoid failing our own allocations. Note, this value is just hardcoded
     * for the amount of slots this initialisation currently uses up.
     * JIRA: CAMKES-204.
     */
    free_slot_start += 16;

    /* We need to be able to actual store caps to the maximum number of objects
     * we may be dealing with.
     * This check can still pass and initialisation fail as we need extra slots for duplicates
     * for CNodes and TCBs.
     */
    ZF_LOGD("free_slot_end = %u, free_slot_start = %u", free_slot_end, free_slot_start);
    ZF_LOGD("# extra caps = %u", CONFIG_CAPDL_LOADER_MAX_OBJECTS);
    assert(free_slot_end - free_slot_start >= CONFIG_CAPDL_LOADER_MAX_OBJECTS);

    ZF_LOGD("  %ld free cap slots, from %ld to %ld\n", (long)(free_slot_end - free_slot_start), (long)free_slot_start, (long)free_slot_end);

    int num_untyped = bootinfo->untyped.end - bootinfo->untyped.start;
    ZF_LOGD("  Untyped memory (%d)\n", num_untyped);
    for (int i = 0; i < num_untyped; i++) {
        uintptr_t ut_paddr = bootinfo->untypedList[i].paddr;
        uintptr_t ut_size = bootinfo->untypedList[i].sizeBits;
        bool ut_isDevice = bootinfo->untypedList[i].isDevice;
        ZF_LOGD("    0x%016" PRIxPTR " - 0x%016" PRIxPTR " (%s)\n", ut_paddr,
                ut_paddr + BIT(ut_size), ut_isDevice ? "device" : "memory");
    }
    ZF_LOGD("Loader is running in domain %d\n", bootinfo->initThreadDomain);
}

static void init_system(void)
{
    bootinfo = platsupport_get_bootinfo();
    simple_t simple;
    simple_default_init_bootinfo(&simple, bootinfo);
    parse_bootinfo(bootinfo);
    init_copy_frame();
}

static int shutdown(int who)
{
    ZF_LOGD("shutting down %d", who);
    component_t *component = &(components[who]);
    seL4_TCB_Suspend(component->tcb);

    ZF_LOGD("delete all pages for this thread");
    int error;
    for (int i = component->ui_vspace_start_cap; i < component->ui_vspace_end_cap; ++i) {
        error = seL4_CNode_Delete(component->cspace, i, CONFIG_WORD_SIZE);
        ZF_LOGF_IFERR(error, "Failed to delete cap");
    }

    ZF_LOGD("delete all other caps");
    for (int i = 0; i < NUM_FREE_SLOTS_MAX; ++i) {
        if (free_slots[i].belongs == who) {
            error = seL4_CNode_Revoke(seL4_CapInitThreadCNode, i, CONFIG_WORD_SIZE);
            ZF_LOGF_IFERR(error, "Failed to revoke cap");

            error = seL4_CNode_Delete(seL4_CapInitThreadCNode, i, CONFIG_WORD_SIZE);
            free_slots[i].belongs = -2;
            ZF_LOGF_IFERR(error, "Failed to delete cap");
        }
    }
    return 0;
}

static int reload(int who)
{
    shutdown(who);
    component_t *component = &(components[who]);

    ZF_LOGD("setup this component %d again", who);
    setup_component(who);

    seL4_TCB_Resume(component->tcb);
    return 0;
}

int main(int argc, char *argv[])
{
    /* _zf_log_output_lvl = 5; */
    platsupport_serial_setup_bootinfo_failsafe();
    ZF_LOGF("hi from provider\n");

    cpio_size = _component_cpio_end - _component_cpio;
    for (int i = 0; i < NUM_FREE_SLOTS_MAX; ++i) {
        free_slots[i].belongs = -2;
    }
    init_system();
    sort_untypeds(bootinfo);

    seL4_DebugDumpScheduler();

    syscall_ep = get_slot(-1);
    create_object(seL4_EndpointObject, 0, seL4_CapInitThreadCNode, syscall_ep);

    setup_components();

    resume_components();
    printf("Done, start handling irqs\n");

    // handling irq syscalls
    seL4_CPtr new_cap = get_slot(-1);
    seL4_CPtr caller = get_slot(-1);

#define RELOADINV nInvocationLabels + 1
#define SHUTDOWNINV nInvocationLabels + 2

    int reload_count = 0;

    while (1) {
        seL4_Word result;
        seL4_SetCapReceivePath(seL4_CapInitThreadCNode, new_cap, seL4_WordBits);
        seL4_Word badge;
        seL4_MessageInfo_t tag = seL4_Recv(syscall_ep, &badge);
        seL4_CNode_SaveCaller(seL4_CapInitThreadCNode, caller, CONFIG_WORD_SIZE);
        seL4_Word invLabel = seL4_MessageInfo_get_label(tag);

        switch (invLabel) {
        case IRQIssueIRQHandler: {
            assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
            seL4_Word mr0 = seL4_GetMR(0);
            seL4_Word mr1 = seL4_GetMR(1);
            seL4_Word mr2 = seL4_GetMR(2);
            result = seL4_IRQControl_Get(seL4_CapIRQControl, mr0, new_cap, mr1, mr2);
            seL4_Send(caller, seL4_MessageInfo_new(result, 0, 0, 0));
            break;
        }
        case RELOADINV: {
            printf("the %dth reload\n", reload_count++);
            curr_untyped = 0;
            result = reload(badge);
            seL4_DebugDumpScheduler();
            break;
        }
        case SHUTDOWNINV: {
            curr_untyped = 0;
            result = shutdown(badge);
            seL4_DebugDumpScheduler();
            break;
        }
        default:
            ZF_LOGF("don't play");
        }

    }
    return 0;
}
