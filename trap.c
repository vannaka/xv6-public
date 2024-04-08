#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

// alarm_exit() start and end addrs. See trapasm.S
extern uint alarm_exit;
extern uint alarm_exit_end;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    struct proc *p = myproc();

    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();

    // If tick occured while in user space
    if(p != 0 && (tf->cs & 3) == DPL_USER)
    {
      #define push_u(x)           \
        ({                        \
          uint v = x;             \
          tf->esp -= 4;           \
        *((uint*)tf->esp) = (v); })
    
      // update alarm ticks
      // NOTE: This attibutes all of the last tick to this proc.
      //  If a proc always runs for less than a tick it will never
      //  have its alarm handler called.
      p->alarm_ticks++;

      // Evaluate alarm period
      if( ( p->alarm_period != 0              )
       && ( p->alarm_ticks >= p->alarm_period ) )
      {
        p->alarm_ticks = 0;

        // Push return address for interrupted user context.
        // alarm_exit()'s ret will return to this address.
        push_u(tf->eip);

        // Save caller-saved regs
        // TODO

        // Place alarm_exit() function onto the stack        
        uint sz = (uint)&alarm_exit_end - (uint)&alarm_exit;
        tf->esp -= sz;

        memmove( (uchar*)tf->esp, &alarm_exit, sz );
        
        // Stuff stack with NOP's to keep 4byte alignment
        for( uint i = 0; i < 4 - (sz & 3 ); i++ )
        {
          tf->esp -= 1;
          *((uchar*)tf->esp) = 0x90; // NOP
        }

        // cprintf("\n\nPlacing alarm_exit() at: [0x%x - 0x%x] ", tf->esp, tf->esp+sz);

        // Push address for alarm_exit().
        // p->alarm_handler()'s ret will return to this address.
        push_u(tf->esp);

        // Execute alarm_handler on iret
        tf->eip = (uint)p->alarm_handler;
      }
    }

    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  case T_PGFLT:
    // Handle page fault in user software only
    if(myproc() && (tf->cs&3) == DPL_USER){
      uint faddr = rcr2();

      // Faulting addr is in address space. alloc new page
      if( faddr < myproc()->sz ){
        faddr = PGROUNDDOWN(faddr);

        allocuvm(myproc()->pgdir, faddr, faddr + PGSIZE);
      }
      
      break;
    }
  // Intentional fallthrough

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
