#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>		/* Needed for the macros */
#include <linux/sched.h>		/* Needed for 'for_each_process' */
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/signal.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/linkage.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <asm/page.h>
#include <linux/rwsem.h>
#include <linux/hugetlb.h>
#include <asm/pgtable_types.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

struct task_struct *task;
int wss; // working set count
int twss = 0; // total WSS
int accessTest(struct vm_area_struct*, unsigned long, pte_t*);
int SLEEP_TIME = 1000;

int my_kthread_function(void* data){

	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	spinlock_t *ptl;
	pte_t *ptep, pte;

	// use PAGE_SIZE constant for the offset
	while (!kthread_should_stop()){

		for_each_process(task){
			twss += wss;
			wss = 0; // every process's WSS gets set to 0 after we count one process
			unsigned long virtAddr;
			// go through the VMAs of a process where virtAddr is the address
			if (task->mm != NULL && task->mm->mmap != NULL){
				struct vm_area_struct *temp = task->mm->mmap;
				while (temp != NULL){
					if (temp->vm_flags && VM_IO){
						for (virtAddr = temp->vm_start; virtAddr < temp->vm_end; virtAddr += PAGE_SIZE){
							pgd = pgd_offset(task->mm, virtAddr);

							// checks if there is a valid entry in the page global directory
							if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))){
								// do nothing
							}
							else{

								pud = pud_offset(pgd, virtAddr);

								// checks if there is a valid entry in the page upper directory
								if (pud_none(*pud) || unlikely(pud_bad(*pud))){
									// do nothing
								}
								else{

									pmd = pmd_offset(pud, virtAddr);

									// checks if there is a valid entry in the page middle directory
									if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))){
										// do nothing
									}
									else{

										ptep = pte_offset_map_lock(task->mm, pmd, virtAddr, &ptl);

										pte = *ptep;

										if (pte_present(pte)){
											accessTest(temp, virtAddr, ptep);
										}

										pte_unmap_unlock(ptep, ptl);
									}
								}
							}
						}
					}

					temp = temp->vm_next;
				}
			}

			if (twss > (totalram_pages * 9) / 10){
				printk(KERN_INFO "Kernel Alert!\n");
			}

			printk(KERN_INFO "PID %d: %d\n", task->pid, wss); // prints [PID]:[WSS] of the process
		}
		twss = 0;
		msleep(SLEEP_TIME);
	}
	return 0;
}

int accessTest(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep){
	int ret;
	ret = 0;

	if (pte_young(*ptep)){
		ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
			(unsigned long *)&ptep->pte);
		wss++;
	}

	if (ret)
		pte_update(vma->vm_mm, addr, ptep);

	return ret;
}

static int __init thrash_detection(void){
	int data = 20;

	// We can instantiate multiple threads, but I think one should suffice?
	task = kthread_run(
		&my_kthread_function,
		(void*)data,
		"my_kthread");

	return 0;
}



static void __exit thrash_detection_exit(void){
	kthread_stop(task);
}

module_init(thrash_detection);
module_exit(thrash_detection_exit);