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

#include <utils/util.h>
#include <sel4/sel4.h>
#include <sel4utils/sel4_zf_logif.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>
#include <sel4/sel4_arch/mapping.h>

extern char bi_frame[];

extern seL4_Word untyped_start;
extern seL4_Word untyped_end;

extern seL4_Word num_untyped_provide;
extern seL4_Word untyped_size_bit;

extern seL4_Word empty_start;
extern seL4_Word empty_end;
static seL4_Word free_slot_start;

extern seL4_Word cnode_size;

extern seL4_Word provider_cnode;

extern seL4_Word roottask_tcb;
extern seL4_Word roottask_cnode;

extern seL4_Word device_untyped;
extern seL4_Word device_untyped_addr;

extern seL4_Word syscall_ep;
extern seL4_Word irq_control;
extern seL4_Word roottask_bi_addr;

static seL4_BootInfo* bi;

extern seL4_Word free_slot_1;
extern seL4_Word free_slot_2;

static void dummy_ui_frames_cap()
{
}

static void move_untyped_cap()
{
    free_slot_start = empty_start;
    bi->untyped.start = empty_start;
    bi->untyped.end = empty_start + num_untyped_provide + 1;
    for (int i = 0; i < num_untyped_provide; ++i) {
        seL4_CNode_Copy(
            roottask_cnode,
            free_slot_start++,
            CONFIG_WORD_SIZE,

            provider_cnode,
            untyped_start + i,
            CONFIG_WORD_SIZE,
            seL4_AllRights
        );
        printf(".\n");

        bi->untypedList[i].sizeBits = untyped_size_bit;
    }

    seL4_CNode_Move(
            roottask_cnode,
            free_slot_start++,
            CONFIG_WORD_SIZE,
            provider_cnode,
            device_untyped,
            CONFIG_WORD_SIZE
            );
    bi->untypedList[num_untyped_provide].sizeBits = 12;
    bi->untypedList[num_untyped_provide].paddr = device_untyped_addr;
    bi->untypedList[num_untyped_provide].isDevice = true;

    bi->empty.start = free_slot_start + 1;
    bi->empty.end = empty_end;
}

static void set_bootinfo_addr()
{
    seL4_UserContext regs = {0};
    seL4_Word error = seL4_TCB_ReadRegisters(roottask_tcb, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    ZF_LOGF_IFERR(error, "Failed to read registers");

    #ifdef CONFIG_ARCH_AARCH32
        regs.r0 = roottask_bi_addr;
    #endif
    #ifdef CONFIG_ARCH_AARCH64
        regs.x0 = roottask_bi_addr;
    #endif
    #ifdef CONFIG_ARCH_IA32
        regs.eax = roottask_bi_addr;
    #endif
    #ifdef CONFIG_ARCH_X86_64
        regs.rdi = roottask_bi_addr;
    #endif
    #ifdef CONFIG_ARCH_RISCV
        regs.a0 = roottask_bi_addr;
    #endif

    error = seL4_TCB_WriteRegisters(roottask_tcb, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    ZF_LOGF_IFERR(error, "Failed to write registers");
}

static void fill_bi()
{
    bi = (void*)bi_frame;
    ZF_LOGD("filling bootinfo frame @ %p", bi);
    bi->nodeID = 0;
    bi->numNodes = 1;
    bi->initThreadDomain = 0;
    bi->initThreadCNodeSizeBits = cnode_size;

    dummy_ui_frames_cap();
    move_untyped_cap();
    set_bootinfo_addr();
}

int main(int argc, char *argv[])
{
    ZF_LOGD("Hi there from BIRQCP");

    fill_bi();

    // handling irq syscalls
    seL4_CPtr new_cap = free_slot_1;
    seL4_CPtr caller = free_slot_2;

    while (1) {
        seL4_Word result;
        seL4_SetCapReceivePath(provider_cnode, new_cap, seL4_WordBits);
        seL4_Word badge;
        seL4_MessageInfo_t tag = seL4_Recv(syscall_ep, &badge);
        seL4_CNode_SaveCaller(provider_cnode, caller, CONFIG_WORD_SIZE);
        seL4_Word invLabel = seL4_MessageInfo_get_label(tag);

        switch (invLabel) {
        case IRQIssueIRQHandler: {
            assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
            seL4_Word mr0 = seL4_GetMR(0);
            seL4_Word mr1 = seL4_GetMR(1);
            seL4_Word mr2 = seL4_GetMR(2);
            result = seL4_IRQControl_Get(irq_control, mr0, new_cap, mr1, mr2);
            seL4_Send(caller, seL4_MessageInfo_new(result, 0, 0, 0));
            break;
        }
        default:
            ZF_LOGF("don't play");
        }

    }
}
