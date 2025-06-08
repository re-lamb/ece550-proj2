/*
 * ECE550 Spring 2025 Project part 3
 * R.E. Lamb, Harsha Duvvuru
 *
 * usage: encodeit [-h] [-s seed] [-n insts] [-t threads] [-l logfile]
 *
 * args:
 *      -h          print usage message
 *      -s seed     set random seed
 *      -n insts    set number to generate
 *		-t threads 	set number of threads
 *		-l logfile	set logfile name
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h> 

#include "ia32_encode.h"

typedef int (*funct_t)();
typedef struct { volatile char *pointer_addr; } test_i;
typedef volatile char *tptrs;

// globals to aid debug to start
volatile char *mptr = 0,*next_ptr = 0,*mdptr = 0, *comm_ptr = 0;
int num_inst = 25;
int nthreads = 1;

int target_ninstrs = MAX_DEF_INSTRS;
int pid_task[MAX_THREADS];

test_i test_info[NUM_PTRS];
volatile char *mptr_threads[MAX_THREADS];
volatile char *mdptr_threads[MAX_THREADS];
volatile char *comm_ptr_threads[MAX_THREADS];

funct_t start_test;

int build_instructions(volatile char*, int, int);
int executeit();

int rand_range(int min_n, int max_n)
{
	return rand() % (max_n - min_n + 1) + min_n;
}

int main(int argc, char *argv[])
{
	int opt, i, pid;
	int seed = 0;
  	int ibuilt = 0;
  	int rc = 0;
  	char* logfile = NULL;
  
  	// parse command line
  	while ((opt = getopt(argc, argv, "hs:n:t:l:")) != -1)
  	{
    	switch (opt)
    	{
      	case 's':
        	seed = strtol(optarg, NULL, 0);
        	break;
        
      	case 'n':
        	num_inst = strtol(optarg, NULL, 0); 
        	break;

        case 't':
        	nthreads = strtol(optarg, NULL, 0);  
        	break;
      
      	case 'l':
      		logfile = optarg;
      		break;

      	case 'h':
      	default:
        	fprintf(stderr, "usage: encodeit [-h] [-s seed] [-n insts] [-t threads] [-l logfile]\n");
        	exit(1);
    	}
  	}

	fprintf(stderr, "seed = %d, num insts = %d, num threads = %d\n", seed, num_inst, nthreads);
	if (logfile) fprintf(stderr, "logfile = \"%s\"\n", logfile); 

	if (nthreads > MAX_THREADS) 
	{
		fprintf(stderr,"Sorry only built for %d threads overriding your %d\n", MAX_THREADS, nthreads);
		fflush(stderr);
		nthreads = MAX_THREADS;
	}

	srand(seed);

	/* allocate buffer to perform stores and loads to  */
	test_info[DATA].pointer_addr = mmap(
		(void *) 0,
		(MAX_DATA_BYTES + PAGESIZE-1) * nthreads,
		PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED,
		0, 0
		);

	/* save the base address for debug like before */
	mdptr = test_info[DATA].pointer_addr;

	if (((int *)test_info[DATA].pointer_addr) == (int *)-1) 
	{
		perror("Couldn't mmap (MAX_DATA_BYTES)");
		exit(1);
	}

	/* allocate buffer to build instructions into */
	test_info[CODE].pointer_addr = mmap(
		(void *) 0,
		(MAX_INSTR_BYTES + PAGESIZE-1) * nthreads,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_ANONYMOUS | MAP_SHARED,
		0, 0
		);

	/* keep a copy to the base here */
	mptr = test_info[CODE].pointer_addr;

	if (((int *)test_info[CODE].pointer_addr) == (int *)-1) 
	{
		perror("Couldn't mmap (MAX_INSTR_BYTES)");
		exit(1);
	}

	/* allocate buffer to build communications area into */
	test_info[COMM].pointer_addr = mmap(
		(void *) 0,
		(MAX_COMM_BYTES + PAGESIZE-1) * nthreads,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_ANONYMOUS | MAP_SHARED,
		0, 0
		);

	comm_ptr = test_info[COMM].pointer_addr;

	if (((int *)test_info[COMM].pointer_addr) == (int *)-1) 
	{
		perror("Couldn't mmap (MAX_COMM_BYTES)");
		exit(1);
	}

	/* make the standard output and stderrr unbuffered */
	setbuf(stdout, (char *) NULL);
	setbuf(stderr, (char *) NULL);

	/* start appropriate # of threads */
	for (i = 0; i < nthreads; i++) 
	{
		next_ptr = (mptr + (i * MAX_INSTR_BYTES));         			// init next_ptr
		fprintf(stderr, "T%d next_ptr = 0x%lx\n", i, (unsigned long)next_ptr);
		mdptr_threads[i] = (tptrs)(mdptr + (i * MAX_DATA_BYTES)); 	// init threads data pointer
		mptr_threads[i] = (tptrs)next_ptr;                     		// save ptr per thread
		comm_ptr_threads[i] = (tptrs)comm_ptr;                 		// everyone gets the same for now

		/* use fork to start a new child process */
		if ((pid = fork()) == 0) 
		{
			fprintf(stderr,"T%d started\n", i);

			//
			// NOTE:  you could set your sched_setaffinity here...better to make a subroutine to bind
			// 
			//
			ibuilt = build_instructions(mptr_threads[i], i, num_inst);  

			/* ok now that I built the critters, time to execute them */
			start_test = (funct_t) mptr_threads[i];
			executeit(start_test);
			fprintf(stderr,"T%d generation program complete, instructions executed: %d\n",i, ibuilt);

			break;	
		}
        else if (pid_task[i] == -1) 
        {
			perror("fork me failed");
			exit(1);
		} 
		else 
		{ 
			// this should be the parent 
			pid_task[i] = pid; // save pid

			fprintf(stderr,"T%d PID = %d\n", i, pid);
		}
	     
	} // end for nthreads


	// wait for threads to complete

	for (i = 0; i < nthreads; i++) 
	{
		waitpid(pid_task[i], NULL, 0);
	}

	// clean up the allocation before getting out
	munmap((caddr_t)mdptr,(MAX_DATA_BYTES + PAGESIZE-1)*nthreads);
	munmap((caddr_t)mptr,(MAX_INSTR_BYTES + PAGESIZE-1)*nthreads);
	munmap((caddr_t)comm_ptr,(MAX_COMM_BYTES + PAGESIZE-1)*nthreads);

	return 0;
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
  volatile int rc = 0;

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
int build_instructions(volatile char *next_ptr, int thread_id, int num_to_build) 
{
  int num_built = 0;
  long limit = (long)mptr_threads[thread_id] + MAX_INSTR_BYTES - SAFETY_MARGIN;
  
  fprintf(stderr,"T%d building instructions\n", thread_id);

  // function preamble 
  next_ptr = add_headeri(next_ptr);
  
  // mov mdptr into rdi
  next_ptr = build_imm_to_register(ISZ_8, (long)mdptr_threads[thread_id], REG_EDI, next_ptr);

  // generate n random instructions
  for (int i = 0; i < num_to_build; i++)
	{
    short size; 
    int src, safe_src, dest, type;
    long imm;
    int disp, disp_type; 

    if ((long)next_ptr >= limit)
    {
      fprintf(stderr,"build instructions: instruction buffer full\n");
      break;
    }
    
    // operand size 1/2/4/8
    size = 1 << rand_range(0, 3);

    // generate 0/8/32 displacements
    switch (rand_range(0, 2))
    {
        case 0:
            disp_type = DISP0_MODRM;
            disp = 0xdeadbeef;
            break;
            
          case 1:
            disp_type = DISP8_MODRM;
            disp = rand_range(0, 127);  				// negative offset falls outside mdptr
            break;
            
          case 2:
            disp_type = DISP32_MODRM;
            disp = rand_range(0, MAX_DATA_BYTES - 8);	// don't store outside buffer!
            break;
          
          default:
            fprintf(stderr,"illegal displacement type\n");
    }
    
    // src reg
    src = rand_range(REG_EAX, REG_EDI);

    // src reg for xchg/xadd - excluding sp and rdi
    while ((safe_src = rand_range(REG_EAX, REG_ESI)) == REG_ESP);
    
    // dest reg, excluding sp and rdi
    while ((dest = rand_range(REG_EAX, REG_ESI)) == REG_ESP);
    
    type = rand_range(REG2REG, SFENCE);
    switch (type)
    {
    	case REG2REG:
        	next_ptr = build_mov_register_to_register(size, src, dest, next_ptr);
        	//fprintf(stderr,"reg2reg: sz %d, src %d, dest %d\n", size, src, dest);
        	break;
        
      	case IMM2REG:
        	imm = rand();
        	if (size == 8) imm = (imm << 32) | rand();
        	next_ptr = build_imm_to_register(size, imm, dest, next_ptr);
        	//fprintf(stderr, "imm2reg: sz %d, imm %ld, dest %d\n", size, imm, dest);
        	break;
        
      	case MEM2REG:
      		next_ptr = build_mov_memory_to_register(size, REG_EDI, dest, disp_type, disp, next_ptr);
      		//fprintf(stderr, "mem2reg: sz %d, dest %d, type %d, disp %x\n", size, dest, disp_type, disp);
      		break;

      	case REG2MEM:
        	next_ptr = build_reg_to_memory(size, src, REG_EDI, disp_type, disp, next_ptr);
        	//fprintf(stderr, "reg2mem: sz %d, src %d, type %d, disp %x\n", size, src, disp_type, disp);
        	break;

    	case XADD:
    	case XCHG:
    		// generate with optional lock
    		short lock = rand_range(0, 1);
			if (type == XADD)
				next_ptr = build_xadd(size, safe_src, REG_EDI, disp_type, disp, lock, next_ptr);
			else
				next_ptr = build_xchg(size, safe_src, REG_EDI, disp_type, disp, lock, next_ptr);
			
			//fprintf(stderr, "%s: sz %d, src %d, type %d, disp %x, lock %d\n", 
			//	   (type == XADD) ? "xadd" : "xchg", size, safe_src, disp_type, disp, lock);
			break;

		case MFENCE:
			next_ptr = build_mfence(next_ptr);
			break;

		case LFENCE:
			next_ptr = build_lfence(next_ptr);
			break;

		case SFENCE:
			next_ptr = build_sfence(next_ptr);
			break;
 
    	default:
        	fprintf(stderr, "illegal instruction type\n");
    }
    
    num_built++;
	}

  // function postamble
  next_ptr = add_endi(next_ptr);

  fprintf(stderr,"built %d instructions, next ptr is now 0x%lx\n", num_built, (long) next_ptr);

  return (num_built);
}