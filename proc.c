/*****************************************************************
*       proc.c - simplified for CPSC405 Lab by Gusty Cooper, University of Mary Washington
*       adapted from MIT xv6 by Zhiyi Huang, hzy@cs.otago.ac.nz, University of Otago
********************************************************************/

/*proc module, contains code to implement processes*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "defs.h"
#include "proc.h"
#include <time.h>
#include <math.h>

static void wakeup1(int chan);

// Dummy lock routines. Not needed for lab
void acquire(int *p) {
    return;
}

void release(int *p) {
    return;
}

// enum procstate for printing
char *procstatep[] = { "UNUSED", "EMPRYO", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE" };

// Table of all processes
struct {
  int lock;   // not used in Lab
  struct proc proc[NPROC];
} ptable;

// Initial process - ascendent of all other processes
static struct proc *initproc;

// Used to allocate process ids - initproc is 1, others are incremented
int nextpid = 1;

// Funtion to use as address of proc's PC
void forkret(void)
{
}

// Funtion to use as address of proc's LR
void trapret(void)
{
}

// Initialize the process table
void pinit(void)
{
  memset(&ptable, 0, sizeof(ptable));
}

// Look in the process table for a process id
// If found, return pointer to proc
// Otherwise return 0.
static struct proc* findproc(int pid)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid == pid)
      return p;
  return 0;
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc* allocproc(void)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  p->context = (struct context*)malloc(sizeof(struct context));
  memset(p->context, 0, sizeof *p->context);
  p->context->pc = (uint)forkret;
  p->context->lr = (uint)trapret;

  return p;
}

// Set up first user process.
int userinit(void)
{
  struct proc *p;
  p = allocproc();
  initproc = p;
  p->sz = PGSIZE;
  strcpy(p->cwd, "/");
  strcpy(p->name, "userinit"); 
  p->state = RUNNING;
  curr_proc = p;
  //p->start_t = 0;
  //p->end_t = 0;
  p->vruntime = 0;
  p->weight = 0;
  return p->pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int Fork(int fork_proc_id)
{
  int pid;
  struct proc *np, *fork_proc;

  // Find current proc
  if ((fork_proc = findproc(fork_proc_id)) == 0)
    return -1;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  np->sz = fork_proc->sz;
  np->parent = fork_proc;
  // Copy files in real code
  strcpy(np->cwd, fork_proc->cwd);
 
  pid = np->pid;
  np->state = RUNNABLE;
  strcpy(np->name, fork_proc->name);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
int Exit(int exit_proc_id)
{
  struct proc *p, *exit_proc;

  // Find current proc
  if ((exit_proc = findproc(exit_proc_id)) == 0)
    return -2;

  if(exit_proc == initproc) {
    printf("initproc exiting\n");
    return -1;
  }

  // Close all open files of exit_proc in real code.

  acquire(&ptable.lock);

  wakeup1(exit_proc->parent->pid);

  // Place abandoned children in ZOMBIE state - HERE
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == exit_proc){
      p->parent = initproc;
      p->state = ZOMBIE;
    }
  }

  exit_proc->state = ZOMBIE;

  // sched();
  release(&ptable.lock);
  return 0;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// Return -2 has children, but not zombie - must keep waiting
// Return -3 if wait_proc_id is not found
int Wait(int wait_proc_id)
{
  struct proc *p, *wait_proc;
  int havekids, pid;

  // Find current proc
  if ((wait_proc = findproc(wait_proc_id)) == 0)
    return -3;

  acquire(&ptable.lock);
  for(;;){ // remove outer loop
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != wait_proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        p->kstack = 0;
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || wait_proc->killed){
      release(&ptable.lock);
      return -1;
    }
    if (havekids) { // children still running
      Sleep(wait_proc_id, wait_proc_id);
      release(&ptable.lock);
      return -2;
    }

  }
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
int Sleep(int sleep_proc_id, int chan)
{
  struct proc *sleep_proc;
  // Find current proc
  if ((sleep_proc = findproc(sleep_proc_id)) == 0)
    return -3;

  sleep_proc->chan = chan;
  sleep_proc->state = SLEEPING;
  return sleep_proc_id;
}

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void wakeup1(int chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}


void Wakeup(int chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}



// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int Kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

//This works because p is just the pid
void scheduler(void){

  	struct proc *p;
  	acquire(&ptable.lock);
  	printf("table created\n");

  	double sched_latency = 48;
  	double min_granularity = 6;
	double weight = 0; 
	double niceness = 0;	
	

	// calculate weight
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){	
		p->weight = 1024/(pow(1.25, niceness));
//		printf("%f weight\n", p->weight);
	}

  	//calculate the total weight
  	double totalWeight = 0;
  	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  		if (p->state == RUNNABLE){
  			totalWeight += p->weight;
		}
	}
	printf("weight calculated\n");

	//find minimum runtime in the table
	double min = ptable.proc->vruntime;
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    	if(p->vruntime < min){
      		min = p->vruntime;
		}
	}
	printf("minimum runtime found\n");
	//totalWeight = 5; trying to figure out why it core dumped-cause totalweight was 0 at the time
	//set time slices
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if (p->state == RUNNABLE){
			printf("runnable\n");
			
			p->timeSlice = p->weight / totalWeight * sched_latency;
			printf("time slice : %d\n", p->timeSlice);
			if (p->timeSlice < min_granularity){
				printf("oops too small\n");
				p->timeSlice = min_granularity;
			}
		}
	}

	printf("time slices set\n");

	//turn off the one that's running
	curr_proc->state = RUNNABLE;
	//curr_proc->end_t = clock();
	//curr_proc->runtime += (double)(curr_proc->end_t-curr_proc->start_t)/CLOCKS_PER_SEC;
 
  
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    
    	curr_proc = p;

	    if(p->state == RUNNABLE && p->vruntime <= min){
    		p->state = RUNNING;
      		p->vruntime += 1;
      		break;
    	}
  
  }
  release(&ptable.lock);

}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid > 0)
      printf("pid: %d, parent: %d state: %s\n", p->pid, p->parent == 0 ? 0 : p->parent->pid, procstatep[p->state]);
}




