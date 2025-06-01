//
// simple encoding example fr IA-32 validation project
//
// project_part2
// 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>

#include "ia32_encode.h"

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define REG2REG   0x0
#define IMM2REG   0x1
#define REG2MEM   0x2
#define MEM2REG   0x3

// typedefs
typedef int (*funct_t)();

// globals to aid debug to start
volatile char *mptr = 0, *next_ptr = 0, *mdptr = 0;
int num_inst = 0;

// forward declarations
int build_instructions();
funct_t start_test;
int executeit();

int rand_range(int min, int max)
{
  return rand() % (max - min + 1) + min;
}

int main(int argc, char *argv[])
{
  int ibuilt = 0, rc = 0;
  
  srand(time(0));
  
  // allocate buffer to perform stores and loads to, and set permissions 
  mdptr = (volatile char *)mmap(
    (void *) 0,
    (MAX_DATA_BYTE + PAGESIZE-1),
    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED,
    0, 0
  );

  if (mdptr == MAP_FAILED) 
  { 
    printf("data mptr allocation failed\n"); 
    exit(1); 
  }
  fprintf(stderr,"mdptr is 0x%lx\n", (long) mdptr);

  // allocate buffer to build instructions into, and set permissions to allow execution of this memory area 
  mptr = (volatile char *)mmap(
    (void *) 0,
    (MAX_INSTR_BYTES + PAGESIZE-1),
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_ANONYMOUS | MAP_SHARED,
    0, 0
  );

  if (mptr == MAP_FAILED) 
  { 
    printf("instr  mptr allocation failed\n"); 
    exit(1); 
  }
  fprintf(stderr,"mptr is 0x%lx\n", (long) mptr);
  
  next_ptr = mptr;                  // init next_ptr
  ibuilt = build_instructions();    // build instructions

  // ok now that I built the critters, time to execute them
  start_test = (funct_t)mptr;
  executeit(start_test);

  fprintf(stderr, "generation program complete, %d instructions generated, and executed\n", ibuilt);

  // clean up the allocation before getting out
  munmap((caddr_t)mdptr, (MAX_DATA_BYTE + PAGESIZE-1));
  munmap((caddr_t)mptr, (MAX_INSTR_BYTES + PAGESIZE-1));
  
  return(0);
}

/*
 * Function: executeit
 * 
 * Description:
 *    This function will start executing at the function address passed into it 
 *    and return an integer return value that will be used to indicate pass(0)/fail(1)
 *
 * Inputs:  
 *    funct_t start_addr :      function pointer 
 *
 * Output:  
 *    int                :      0 for pass, 1 for fail
 */
int executeit(funct_t start_addr) 
{
  volatile int i, rc = 0;
  i = 0;

  rc = (*start_addr)();

  return(0);
}

static inline volatile char *add_headeri(volatile char *tgt_addr)
{
  // setup stack
  tgt_addr = build_enter(2048, tgt_addr);
  
  // save caller regs
  tgt_addr = build_push_reg(REG_EBX, 0, tgt_addr);
  tgt_addr = build_push_reg(REG_EBP, 0, tgt_addr);
  tgt_addr = build_push_reg(REG_R12, 1, tgt_addr);
  tgt_addr = build_push_reg(REG_R13, 1, tgt_addr);
  tgt_addr = build_push_reg(REG_R14, 1, tgt_addr);
  tgt_addr = build_push_reg(REG_R15, 1, tgt_addr);
  
  return(tgt_addr);
}

static inline volatile char *add_endi(volatile char *tgt_addr)
{
  // restore regs
  tgt_addr = build_pop_reg(REG_R15, 1, tgt_addr);
  tgt_addr = build_pop_reg(REG_R14, 1, tgt_addr);
  tgt_addr = build_pop_reg(REG_R13, 1, tgt_addr);
  tgt_addr = build_pop_reg(REG_R12, 1, tgt_addr);
  tgt_addr = build_pop_reg(REG_EBP, 0, tgt_addr);
  tgt_addr = build_pop_reg(REG_EBX, 0, tgt_addr);
  
  // break down stack
  tgt_addr = build_leave(tgt_addr);
  tgt_addr = build_return(tgt_addr);
  
  return(tgt_addr);
}

/*
 * Function: build_instructions
 *
 * Description:
 *    builds pop register
 *
 * Inputs: 
 *    int reg_index         :  index of register     // TO DO
 *
 * Output: 
 *    int                   :   number of instructions generated
 */
int build_instructions() 
{
  // TO DO: why the strange byte limit?
  long limit = (long)mptr + MAX_INSTR_BYTES;
  
  // function preamble 
  // TO DO: account for header inst count?
  next_ptr = add_headeri(next_ptr);
  
  // mov mdptr into rdi
  next_ptr = build_imm_to_register(ISZ_8, (long)mdptr, REG_EDI, next_ptr);
  
  // generate n random instructions
  for (int i = 0; i < 4000; i++)
  {
    short size; 
    int src, dest, type;
    long imm;
     
    if ((long)next_ptr >= limit)
    {
      fprintf(stderr,"build instructions: instruction buffer full\n");
      break;
    }
    
    // operand size 1/2/4/8
    size = 1 << rand_range(0, 3);
    
    // src reg
    src = rand_range(REG_EAX, REG_EDI);
    
    // dest reg, excluding sp and rdi
    while ((dest = rand_range(REG_EAX, REG_ESI)) == REG_ESP);
    
    type = rand_range(REG2REG, REG2MEM);
    switch (type)
    {
      case REG2REG:
        next_ptr = build_mov_register_to_register(size, src, dest, next_ptr);
        fprintf(stderr,"reg2reg: sz %d, src %d, dest %d\n", size, src, dest);
        break;
        
      case IMM2REG:
        imm = rand();
        if (size == 8) imm = (imm << 32) | rand();
        next_ptr = build_imm_to_register(size, imm, dest, next_ptr);
        fprintf(stderr,"imm2reg: sz %d, imm %ld, dest %d\n", size, imm, dest);
        break;
        
      case REG2MEM:
        int disp, disp_type;

        switch (rand_range(0, 2))
        {
          case 0:
            disp_type = DISP0_MODRM;
            disp = 0xdeadbeef;
            break;
            
          case 1:
            disp_type = DISP8_MODRM;
            disp = rand_range(0, 127);  // negative offset falls outside mdptr
            break;
            
          case 2:
            disp_type = DISP32_MODRM;
            disp = rand_range(0, MAX_DATA_BYTE);
            break;
          
          default:
            fprintf(stderr,"illegal displacement type\n");
        }
        
        next_ptr = build_reg_to_memory(size, src, REG_EDI, disp_type, disp, next_ptr);
        fprintf(stderr,"reg2mem: sz %d, src %d, type %d, disp %x\n", size, src, disp_type, disp);
        break;
        
      default:
        fprintf(stderr,"illegal instruction type\n");
    }
    
    num_inst++;
  }

  // function postamble
  next_ptr = add_endi(next_ptr);

  fprintf(stderr,"next ptr is now 0x%lx\n", (long) next_ptr);

  return (num_inst);
}
