#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>


#include "opt-A2.h"

#include <mips/trapframe.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <mips/types.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;

#if OPT_A2
  for (size_t i = 0 ; i < array_num(p->children); i++){
    struct proc *child = (struct proc *) array_get(p->children, i);
    if (child->status == 0){
      array_remove(p->children, i);
      proc_destroy(child);
      i -= 1; // do not update the index
    }
  }
  int can_fully_delete = 0;
  if (!p->parent || !p->parent->status){
    can_fully_delete = true;
  }

#else
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
#endif /* OPT_A2 */
	
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
#if OPT_A2
	if (can_fully_delete) {
		proc_destroy(p);
	} else {
		p->exit_code = exitcode;
		p->status = 0;
        cv_signal(p->parent_cv, p->lk);
	}
#else
  proc_destroy(p);
#endif /* OPT_A2 */
	
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
  
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
  *retval = curproc->pid;
#else
  *retval = -1;
#endif /* OPT_A2 */
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  if (options != 0) {
    return(EINVAL);
  }

#if OPT_A2
  for (size_t i = 0; i < array_num(curproc->children); i++){
    struct proc* child = (struct proc *) array_get(curproc->children, i);
    if (pid == child->pid) {
        lock_acquire(child->lk);
        // if waitpid is called before the child process exits
        while (child->status == 1){
            cv_wait(child->parent_cv, child->lk);
        }
        exitstatus = _MKWAIT_EXIT(child->exit_code);
        lock_release(child->lk);
        break; // since pids are unique
    }
  }
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif /* OPT_A2 */

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}   

#if OPT_A2
int sys_fork(struct trapframe *tf, pid_t *retval) {
    int err = 1; // record the error code in this function.

    // Create a new process structure for the child process.
    struct proc *child = proc_create_runprogram("newproc");

    // Create and copy the address space (and data) from the parent to the child.
    struct addrspace* as = NULL;
    err = as_copy(curproc_getas(), &as);
    
    // Attach the newly created address space to the child process structure.
    child->p_addrspace = as;

    // Create the parent/child relationship.
    child->parent = curproc;

    err = array_add(curproc->children, (void *) child, NULL);

    struct trapframe* tf_parent = kmalloc(sizeof(struct trapframe));
    *tf_parent = *tf;

    // Create a thread for child process. 
    err = thread_fork("newthread", child, enter_forked_process, (void *) tf_parent, 0);
    
    *retval = child->pid;

    return 0;
}
#endif /* OPT_A2 */
