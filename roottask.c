#include <stdio.h>
#include <sel4/sel4.h>
#include <utils/util.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>
#include <sel4platsupport/platsupport.h>
#include <simple-default/simple-default.h>
#include <sel4utils/sel4_zf_logif.h>

int main(int argc, char *argv[])
{
    platsupport_serial_setup_bootinfo_failsafe();
    printf("Hello from roottask\n");
    return 0;
}
