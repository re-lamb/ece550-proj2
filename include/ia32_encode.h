/*
 * ECE550 Spring 2025 Project part 3
 * R.E. Lamb, Harsha Duvvuru
 *
 *
 * GENERAL Instruction Format
 *
 * -----------------------------------------------------------------
 * | Instruction    |   Opcode | ModR/M | Displacement | Immediate |
 * | Prefix         |          |        |              |           |
 * -----------------------------------------------------------------
 *
 *  7  6  5   3  2   0
 * --------------------
 * | Mod | Reg* | R/M |
 * --------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <limits.h>

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

// for random generation
#define REG2REG     0
#define IMM2REG     1
#define REG2MEM     2
#define MEM2REG     3
#define XADD        4
#define XCHG        5
#define MFENCE      6
#define LFENCE      7
#define SFENCE      8

// ~largest encodable instruction + postamble
#define SAFETY_MARGIN    32

/*
 * definitions we need to support these functions.
 *
 * see Table 2-2 in SDM for register/MODRM encode usage
 *
 */
#define DISP0_MODRM    0x00
#define DISP8_MODRM    0x40
#define DISP32_MODRM   0x80
#define BASE_MODRM     0xc0
#define REG_SHIFT      0x3
#define MODRM_SHIFT    0x6
#define RM_SHIFT       0x0
#define REG_MASK       0x7
#define RM_MASK        0x7
#define MOD_MASK       0x3

// register defs based on MOD RM table
#define REG_EAX        0x0
#define REG_ECX        0x1
#define REG_EDX        0x2
#define REG_EBX        0x3
#define REG_ESP        0x4
#define REG_EBP        0x5
#define REG_ESI        0x6
#define REG_EDI        0x7

// register defs based for x86_64, requires REX extension
#define REG_R8        0x0
#define REG_R9        0x1
#define REG_R10       0x2
#define REG_R11       0x3
#define REG_R12       0x4
#define REG_R13       0x5
#define REG_R14       0x6
#define REG_R15       0x7

// byte offset
#define BYTE1_OFF      0x1
#define BYTE2_OFF      0x2
#define BYTE3_OFF      0x3
#define BYTE4_OFF      0x4
#define BYTE8_OFF      0x8

// ISIZE (instruction size in bytes, for move example 2byte = 16bit)

#define ISZ_1         0x1
#define ISZ_2         0x2
#define ISZ_4         0x4
#define ISZ_8         0x8

// x86_64 support defines
#define PREFIX_16BIT  0x66
#define PREFIX_LOCK   0xf0
#define REX_PREFIX    0x40
#define REX_W         0x8
#define REX_R         0x4
#define REX_X         0x2
#define REX_B         0x1

// code generation defines
#define MAX_THREADS     4
#define MAX_INSTR_BYTES (3 * PAGESIZE)      // allocate 3  PAGES for instruction
#define MAX_DATA_BYTES  (10 * PAGESIZE)     // allocate 10 PAGES for data

// information sharing between tasks
#define NUM_PTRS 2
#define CODE 0
#define DATA 1

/*
 * Function: build_mov_register_to_register
 *
 * Inputs: 
 *    short mov_size               :  size of the move being requested
 *    int   src_reg                :  register source encoding 
 *    int   dest_reg               :  destination reg of move
 *    volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 */
static inline volatile char *build_mov_register_to_register(short mov_size, int src_reg, int dest_reg, volatile char *tgt_addr)
{
  switch(mov_size)  
  {
    case ISZ_1: 
      (*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8a;
      tgt_addr += BYTE2_OFF;
      break;

    case ISZ_2:
      *tgt_addr = PREFIX_16BIT;
      tgt_addr += BYTE1_OFF;
      // FALL THROUGH
      
    case ISZ_4:
      (*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8b;
      tgt_addr += BYTE2_OFF;
      break;
      
    case ISZ_8:
      *tgt_addr = (REX_PREFIX | REX_W);
      tgt_addr += BYTE1_OFF;
      (*(short *) tgt_addr) = ((BASE_MODRM) + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8b;
      tgt_addr += BYTE2_OFF;
      break;
    
    default:
      fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to register move\n", mov_size);
      exit(-1);
  }
  
  return(tgt_addr);
}

/*
 * Function: build_mov_imm_to_register
 *
 * Inputs: 
 *    short mov_size               :  size of the move being requested
 *    int   imm                    :  immediate value 
 *    int   dest_reg               :  destination reg of move
 *    volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 */
static inline volatile char *build_imm_to_register(short mov_size, long imm, int dest_reg, volatile char *tgt_addr)
{
  switch(mov_size)  
  {
    case ISZ_1: 
      (*(short *) tgt_addr) = (BASE_MODRM + dest_reg) << 8 | 0xc6;
      tgt_addr += BYTE2_OFF;
      *tgt_addr = (char)imm;
      tgt_addr += BYTE1_OFF;
      break;

    case ISZ_2:
      *tgt_addr = PREFIX_16BIT;
      tgt_addr += BYTE1_OFF;
      (*(short *) tgt_addr) = (BASE_MODRM + dest_reg) << 8 | 0xc7;
      tgt_addr += BYTE2_OFF;
      (*(short *) tgt_addr) = (short)imm;
      tgt_addr += BYTE2_OFF;
      break;
      
    case ISZ_4:
      (*(short *) tgt_addr) = (BASE_MODRM + dest_reg) << 8 | 0xc7;
      tgt_addr += BYTE2_OFF;
      (*(int *) tgt_addr) = (int)imm;
      tgt_addr += BYTE4_OFF;
      break;
      
    case ISZ_8:
      *tgt_addr = (REX_PREFIX | REX_W);
      tgt_addr += BYTE1_OFF;
      *tgt_addr = (0xb8 + dest_reg);
      tgt_addr += BYTE1_OFF;
      (*(long *) tgt_addr) = imm;
      tgt_addr += BYTE8_OFF;
      break;
      
    default:
      fprintf(stderr,"ERROR: Incorrect size (%d) passed to immediate to register move\n", mov_size);
      exit(-2);
  }
  
  return(tgt_addr);
}

/*
 * Function: build_reg_to_memory
 *
 * Inputs: 
 *    short mov_size               :  size of the move being requested
 *    int   src_reg                :  register source encoding 
 *    int   dest_reg               :  destination reg of move
 *    unsigned char disp_type      :  0/8/32-bit displacement type
 *    int   disp                   :  displacement value
 *    volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 */
static inline volatile char *build_reg_to_memory(short mov_size, int src_reg, int dest_reg, unsigned char disp_type, int disp, volatile char *tgt_addr)
{
  switch(mov_size)  
  {
    case ISZ_1: 
      (*(short *) tgt_addr) = (disp_type + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x88;
      tgt_addr += BYTE2_OFF;
      break;

    case ISZ_2:
      *tgt_addr = PREFIX_16BIT;
      tgt_addr += BYTE1_OFF;
      // FALL THROUGH
      
    case ISZ_4:
      (*(short *) tgt_addr) = (disp_type + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
      tgt_addr += BYTE2_OFF;
      break;
    
    case ISZ_8:
      *tgt_addr = (REX_PREFIX | REX_W);
      tgt_addr += BYTE1_OFF;
      (*(short *) tgt_addr) = (disp_type + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x89;
      tgt_addr += BYTE2_OFF;
      break;
    
    default:
      fprintf(stderr,"ERROR: Incorrect size (%d) passed to register to mem move\n", mov_size);
      exit(-3);
  }
  
  switch(disp_type)
  {
    case DISP0_MODRM:
      // no displacement
      break;
      
    case DISP8_MODRM:
      *tgt_addr = (char)disp;
      tgt_addr += BYTE1_OFF;
      break;
      
    case DISP32_MODRM:
      (*(int *) tgt_addr) = (int)disp;
      tgt_addr += BYTE4_OFF;
      break;
      
    default:
      fprintf(stderr,"ERROR: Invalid displacement (%d) passed to register to memory move\n", disp_type);
      exit(-3);
  }
  
  return(tgt_addr);
}

static inline volatile char *build_mov_memory_to_register(short mov_size, int src_reg, int dest_reg, unsigned char disp_type, int disp, volatile char *tgt_addr)
{
  switch(mov_size)  
  {
    case ISZ_1: 
      (*(short *) tgt_addr) = (disp_type + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8a;
      tgt_addr += BYTE2_OFF;
      break;

    case ISZ_2:
      (*tgt_addr) = PREFIX_16BIT;
      tgt_addr += BYTE1_OFF;
      // FALL THROUGH
      
    case ISZ_4:
      (*(short *) tgt_addr) = (disp_type + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8b;
      tgt_addr += BYTE2_OFF;
      break;
      
    case ISZ_8:
      (*(char *) tgt_addr) = (REX_PREFIX | REX_W);
      tgt_addr += BYTE1_OFF;
      (*(short *) tgt_addr) = (disp_type + (dest_reg << REG_SHIFT) + src_reg) << 8 | 0x8b;
      tgt_addr += BYTE2_OFF;
      break;
    
    default:
      fprintf(stderr,"ERROR: Incorrect size (%d) passed to memory to register move\n", mov_size);
      exit(-1);
  }
  
  switch(disp_type)
  {
    case DISP0_MODRM:
      // no displacement
      break;
      
    case DISP8_MODRM:
      (*(char *) tgt_addr) = (char)disp;
      tgt_addr += BYTE1_OFF;
      break;
      
    case DISP32_MODRM:
      (*(int *) tgt_addr) = (int)disp;
      tgt_addr += BYTE4_OFF;
      break;
      
    default:
      fprintf(stderr,"ERROR: Invalid displacement (%d) passed to memory to register move\n", disp_type);
      exit(-3);
  }
  
  return(tgt_addr);
}

static inline volatile char *build_xadd(short size, int src_reg, int dest_reg, unsigned char disp_type, int disp, short lock, volatile char *tgt_addr)
/*
 * Function: build_xadd
 *
 * Description:
 *    build register to memory xadd with displacement and optional lock prefix
 *
 * Inputs: 
 *    short size                    :  size of the move being requested
 *    int   src_reg                :  register source encoding 
 *    int   dest_reg               :  destination reg of move
 *    unsigned char disp_type      :  0/8/32-bit displacement type
 *    int   disp                   :  displacement value
 *      short lock                  :  include LOCK prefix
 *    volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 */
{
  if (lock)
  {
    *tgt_addr = PREFIX_LOCK;
    tgt_addr += BYTE1_OFF; 
  }

  switch(size)  
  {
    case ISZ_1: 
      (*(short *) tgt_addr) = 0xc00f;
      tgt_addr += BYTE2_OFF;
      break;

    case ISZ_2:
      *tgt_addr = PREFIX_16BIT;
      tgt_addr += BYTE1_OFF;
      // FALL THROUGH
      
    case ISZ_4:
      (*(short *) tgt_addr) = 0xc10f;
      tgt_addr += BYTE2_OFF;
      break;
    
    case ISZ_8:
      *tgt_addr = (REX_PREFIX | REX_W);
      tgt_addr += BYTE1_OFF;
      (*(short *) tgt_addr) = 0xc10f;
      tgt_addr += BYTE2_OFF;
      break;
    
      default:
        fprintf(stderr,"ERROR: Incorrect size (%d) passed to xadd\n", size);
        exit(-4);
    }

    // same ModR/M byte for all
    *tgt_addr = (disp_type + (src_reg << REG_SHIFT) + dest_reg);
    tgt_addr += BYTE1_OFF;
  
    switch(disp_type)
    {
      case DISP0_MODRM:
        // no displacement
        break;
      
      case DISP8_MODRM:
        *tgt_addr = (char)disp;
        tgt_addr += BYTE1_OFF;
        break;
      
      case DISP32_MODRM:
        (*(int *) tgt_addr) = (int)disp;
        tgt_addr += BYTE4_OFF;
        break;
      
      default:
        fprintf(stderr,"ERROR: Invalid displacement (%d) passed to xadd\n", disp_type);
        exit(-3);
    }
    
    return(tgt_addr);
}

static inline volatile char *build_xchg(short size, int src_reg, int dest_reg, unsigned char disp_type, int disp, short lock, volatile char *tgt_addr)
/*
 * Function: build_xchg
 *
 * Description:
 *    build register to memory xchg with displacement and optional lock prefix
 *
 * Inputs: 
 *    short size                  :  size of the move being requested
 *    int   src_reg               :  register source encoding 
 *    int   dest_reg              :  destination reg of move
 *    unsigned char disp_type     :  0/8/32-bit displacement type
 *    int   disp                  :  displacement value
 *    short lock                  :  include LOCK prefix
 *    volatile char *tgt_addr     :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 */
{
  if (lock)
  {
    *tgt_addr = PREFIX_LOCK;
    tgt_addr += BYTE1_OFF; 
  }

  switch(size)  
  {
    case ISZ_1: 
      (*(short *) tgt_addr) = (disp_type + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x86;
      tgt_addr += BYTE2_OFF;
      break;

    case ISZ_2:
      *tgt_addr = PREFIX_16BIT;
      tgt_addr += BYTE1_OFF;
      // FALL THROUGH
      
    case ISZ_4:
      (*(short *) tgt_addr) = (disp_type + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x87;
      tgt_addr += BYTE2_OFF;
      break;
    
    case ISZ_8:
      *tgt_addr = (REX_PREFIX | REX_W);
      tgt_addr += BYTE1_OFF;
      (*(short *) tgt_addr) = (disp_type + (src_reg << REG_SHIFT) + dest_reg) << 8 | 0x87;
      tgt_addr += BYTE2_OFF;
      break;
    
    default:
      fprintf(stderr,"ERROR: Incorrect size (%d) passed to xchg\n", size);
      exit(-5);
  }
  
  switch(disp_type)
  {
    case DISP0_MODRM:
      // no displacement
      break;
      
    case DISP8_MODRM:
      *tgt_addr = (char)disp;
      tgt_addr += BYTE1_OFF;
      break;
      
    case DISP32_MODRM:
      (*(int *) tgt_addr) = (int)disp;
      tgt_addr += BYTE4_OFF;
      break;
      
    default:
      fprintf(stderr,"ERROR: Invalid displacement (%d) passed to xchg\n", disp_type);
      exit(-5);
  }
    
  return(tgt_addr);
}

static inline volatile char *build_enter(short size, volatile char *tgt_addr)
{
  // mode 0 enter instruction with immediate
  (*(int *) tgt_addr) = (size << 8 ) | 0xc8;
  tgt_addr += BYTE4_OFF;
    
  return(tgt_addr);
}

/*
 * Function: build_push_reg
 *
 * Inputs: 
 *    int reg_index                :  index of register
 *    int x86_64f                  :  flag to indicate if we need to extend to rex format
 *    volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 *
 */
static inline volatile char *build_push_reg(int reg_index, int x86_64f, volatile char *tgt_addr)
{
  if (x86_64f) 
  {
    *tgt_addr++ = (REX_PREFIX | REX_B);
  }
  *tgt_addr++ = 0x50 + reg_index;
            
  return(tgt_addr);
}

/*
 * Function: build_pop_reg
 *
 * Inputs: 
 *    int reg_index                :  index of register
 *    int x86_64f                  :  flag to indicate if we need to exted to rex format
 *    volatile char *tgt_addr      :  starting memory address of where to store instruction
 *
 * Output: 
 *    returns adjusted address after encoding instruction
 */
static inline volatile char *build_pop_reg(int reg_index, int x86_64f, volatile char *tgt_addr)
{
  if (x86_64f) 
  {
    *tgt_addr++ = (REX_PREFIX | REX_B);
  }
  *tgt_addr++ = 0x58 + reg_index;
            
  return(tgt_addr);
}

static inline volatile char *build_leave(volatile char *tgt_addr)
{
  *tgt_addr++ = 0xc9;
    
  return(tgt_addr);
}

static inline volatile char *build_return(volatile char *tgt_addr)
{
  *tgt_addr++ = 0xc3;
    
  return(tgt_addr);
}

static inline volatile char *build_mfence(volatile char *tgt_addr)
{
  *tgt_addr++ = 0x0f;
  *tgt_addr++ = 0xae;
  *tgt_addr++ = 0xf0;
    
  return(tgt_addr);
}

static inline volatile char *build_lfence(volatile char *tgt_addr)
{
  *tgt_addr++ = 0x0f; 
  *tgt_addr++ = 0xae;
  *tgt_addr++ = 0xe8;
    
  return(tgt_addr);
}

static inline volatile char *build_sfence(volatile char *tgt_addr)
{
  *tgt_addr++ = 0x0f;
  *tgt_addr++ = 0xae;
  *tgt_addr++ = 0xf8;
    
  return(tgt_addr);
}
