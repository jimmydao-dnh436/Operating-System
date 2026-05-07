#include "common.h"
#include "syscall.h"
#include "stdio.h"

int __sys_xxxhandler(struct krnl_t *knrl, uint32_t pid, struct sc_regs* reg)
{
/* TODO:implementsyscalljob */
printf("Thefirstsystemcallparameter%d\n",reg->a1);
return 0;
}