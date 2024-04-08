#include "mmu.h"

  # vectors.S sends all traps here.
.globl alltraps
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  pushl %esp
  call trap
  addl $4, %esp

  # Return falls through to trapret...
.globl trapret
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  # trapno and errcode
  iret

// .global alarm_enter


# alarm_exit()
.globl alarm_exit
alarm_exit:
  pushl %eax
  pushl %ebx

  // Remove alarm_exit() from stack.
  movl $(alarm_exit_end - alarm_exit), %eax // eax = sz
  addl %eax, %esp

  // Remove alignment NOPs from stack
  andl $3, %eax
  movl $4, %ebx
  sub %ebx, %eax // ebx = 4 - (sz & 3)
  addl %ebx, %esp

  // Remove Caller-saved registers
  // popl %edi
  // popl %esi
  // popl %ebx
  // popl %ebp

  popl %ebx
  popl %eax
  ret
.globl alarm_exit_end
alarm_exit_end:
