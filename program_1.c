#include <stdio.h>
#include <sel4/sel4.h>
#include <utils/util.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>
#include <sel4platsupport/platsupport.h>
#include <simple-default/simple-default.h>
#include <sel4utils/sel4_zf_logif.h>

void sayhello();

int main(int argc, char *argv[])
{
    sayhello();

    return 0;
}

void sayhello()
{
    printf("This is the sayhello program from another capdl loader and then i will say: \n");
    printf("hello\n");
    printf("After me saying hello, the timer client from another capdl loader should resume after 3 second sleep\n");
    while (1) {}
}
