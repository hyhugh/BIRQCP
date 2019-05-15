
#include <stdio.h>
#include <assert.h>
#include <sel4/sel4.h>
#include <platsupport/plat/timer.h>

// CSlots pre-initialised in this CSpace
extern seL4_CPtr endpoint;
// capability to a reply object
extern seL4_CPtr ntfn;
// capability to the device untyped for the timer

extern seL4_CPtr device_untyped;
// empty cslot for the frame
extern seL4_CPtr timer_frame;
// cnode of this process
extern seL4_CPtr cnode;
// vspace of this process
extern seL4_CPtr vspace;
// frame to map the timer to
extern seL4_CPtr frame;
extern const char timer_vaddr[4096];
// irq control capability
extern seL4_CPtr irq_control;
// empty slot for the irq
extern seL4_CPtr irq_handler;

/* constants */
#define EP_BADGE 61 // arbitrary (but unique) number for a badge
#define MSG_DATA 0x6161 // arbitrary data to send

int main(void) {
    /* wait for a message */
    seL4_Word sender_badge;
    seL4_MessageInfo_t tag = seL4_Recv(endpoint, &sender_badge);

    /* make sure the message is what we expected */
    assert(sender_badge == EP_BADGE);
    assert(seL4_MessageInfo_get_length(tag) == 1);

    /* get the message stored in the first message register */
    seL4_Word msg = seL4_GetMR(0);
    printf("timer: got a message from %u to sleep %zu seconds\n", sender_badge, msg);

    /* retype the device untyped into a frame */
    seL4_Error error = seL4_Untyped_Retype(device_untyped, seL4_ARM_SmallPageObject, 0,
                                          cnode, 0, 0, timer_frame, 1);
    ZF_LOGF_IF(error, "Failed to retype device untyped");

    /* unmap the existing frame mapped at vaddr so we can map the timer here */
    error = seL4_ARM_Page_Unmap(frame);
    ZF_LOGF_IF(error, "Failed to unmap frame");

    /* map the device frame into the address space */
    error = seL4_ARM_Page_Map(timer_frame, vspace, (seL4_Word) timer_vaddr, seL4_AllRights, 0);
    ZF_LOGF_IF(error, "Failed to map device frame");
    ttc_t ttc;
    ttc_config_t ttc_config = {
        .vaddr =  (void *) timer_vaddr,
        .id = TTC0_TIMER1
    };

    /* put the interrupt handle for TTC0_TIMER1_IRQ in the irq_handler cslot */
    error = seL4_IRQControl_Get(irq_control, TTC0_TIMER1_IRQ, cnode, irq_handler, seL4_WordBits);
    ZF_LOGF_IF(error, "Failed to get irq capability");

    /* set ntfn as the notification for irq_handler */
    error =  seL4_IRQHandler_SetNotification(irq_handler, ntfn);
    ZF_LOGF_IF(error, "Failed to set notification");

    /* set up the timer driver */
    int timer_err = ttc_init(&ttc, ttc_config);
    ZF_LOGF_IF(timer_err, "Failed to init timer");

    timer_err = ttc_start(&ttc);
    ZF_LOGF_IF(timer_err, "Failed to start timer");

    /* ack the irq in case of any pending interrupts int the driver */
    error = seL4_IRQHandler_Ack(irq_handler);
    ZF_LOGF_IF(error, "Failed to ack irq");

    timer_err = ttc_set_timeout(&ttc, NS_IN_MS, true);
    ZF_LOGF_IF(timer_err, "Failed to set timeout");

    int count = 0;
    while (1) {
        /* Handle the timer interrupt */
        seL4_Word badge;
        seL4_Wait(ntfn, &badge);
        ttc_handle_irq(&ttc);
        if (count == 0) {
            printf("Tick\n");
        }

        /* ack the interrupt */
        error = seL4_IRQHandler_Ack(irq_handler);
        ZF_LOGF_IF(error, "Failed to ack irq");

        count++;
        if (count == 1000 * msg) {
            break;
        }
    }

    /* get the current time */
    uint64_t time = ttc_get_time(&ttc);

    // stop the timer
    ttc_stop(&ttc);

   /* modify the message */
    msg = (uint32_t) time;
    seL4_SetMR(0, msg);

    /* send the modified message back */
    seL4_ReplyRecv(endpoint, tag, &sender_badge);

    return 0;
}

