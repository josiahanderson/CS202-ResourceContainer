///////////////////////////////////////////////////////////////////////////////
//                     University of California, Riverside
//
//
//
//                             Copyright 2021
//
///////////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
///////////////////////////////////////////////////////////////////////////////
//
//   Author:  Josiah Anderson, Tyler Woods, Hung-Wei Tseng, Yu-Chia Liu
//
//   Description:
//     Core of Kernel Module for CSE202's Resource Container Project
//
///////////////////////////////////////////////////////////////////////////////

#include "resource_container.h"

//#include <asm/uaccess.h>
#include <asm/segment.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>


#define DEBUG_LEVEL_GRANULAR 4
#define DEBUG_LEVEL_FUNCTION 3
#define DEBUG_LEVEL_APP 2
#define DEBUG_LEVEL_BENCHMARK 1
#define DEBUG_LEVEL_OFF 0

#define DEBUG_LEVEL 4

static DEFINE_MUTEX(ioctl_lock);
/**
 * Data structures
 */

struct task_object {
    __u64 task_id;
    struct task_struct *current_task;
    struct task_object *next_task;
};

struct share_object {
	__u64 oid;
	unsigned long pfn;
	char* address;
	struct share_object *next;
};

struct lock_object {
	__u64 oid;
	struct mutex mutex_lock;
	struct lock_object *next;
};

struct container_list {
    __u64 container_id;
    struct task_object *tasks;
	struct share_object *objects;
	struct lock_object *locks;
    struct container_list *next_container;
	struct mutex *container_lock;
} *container_head = NULL;


/**
 * Helper functions
 */

struct container_list *find_container(__u64 container_id) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering find_container with input: cid %d\n", (int)container_id);
	#endif

	struct container_list *current_container = container_head;
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering while loop found in find_container\n");
	#endif
	while (current_container != NULL) {
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Looking for cid: %d found: %d in find_container\n", (int)container_id, (int)current_container->container_id);
		#endif
		if (current_container->container_id == container_id) {
			return current_container;
		}
		current_container = current_container->next_container;
	}
	printk("find_container: Container not found with cid: %d\n", (int)container_id);
	return NULL;
}

struct container_list *VM_find_container(struct task_struct *curr) {
	struct container_list *current_container = container_head;
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Looking for pid: %d in VM_find_container\n", (int)curr->pid);
	#endif
	
	while(current_container != NULL) {
		struct task_object *current_task_object = current_container->tasks;
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering while loop found in VM_find_container\n");
		#endif
		while(current_task_object != NULL) {
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Looking for pid: %d found: %d in VM_find_container\n", (int)current_task_object->current_task->pid, (int)curr->pid);
			#endif
			if(current_task_object->current_task->pid == curr->pid) {
				return current_container;
			}
			current_task_object = current_task_object->next_task;
		}
		current_container = current_container->next_container;
	}
	printk("VM_find_container: Container not found with pid: %ld\n", (long)curr->pid);
	return NULL;
}

struct share_object *find_object(__u64 oid, struct container_list *container) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering find_object with input: oid %d\n", (int)oid);
	#endif
	struct container_list *current_container = container;
	struct share_object *current_object = current_container->objects;
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering while loop found in find_object\n");
	#endif
	while (current_object != NULL) {
		
		if (current_object->oid == oid) {
			return current_object;
		}
		current_object = current_object->next;
	}
	printk("find_object: Object not found with oid: %d\n", (int)oid);
	return NULL;
}

struct lock_object *find_lock(__u64 oid, struct container_list *container) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering find_lock with input: oid %d\n", (int)oid);
	#endif
	struct container_list *current_container = container;
	struct lock_object *current_lock = NULL;
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering while loop found in find_lock\n");
	#endif
	current_lock = current_container->locks;
	while (current_lock != NULL) {
		
		if (current_lock->oid == oid) {
			return current_lock;
		}
		current_lock = current_lock->next;
	}
	printk("find_lock: Lock not found with oid: %d\n", (int)oid);
	return NULL;
}

struct task_object *find_task(struct container_list *container, int task_id) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering find_task with input: task_id %d\n", task_id);
	#endif

	if (container != NULL) {

		struct task_object *current_task = container->tasks;

		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering while loop found in find_task\n");
		#endif
		struct task_object *start_task = current_task;
		while (current_task != NULL) {

			if (current_task->task_id == task_id) {
				return current_task;
			}
			current_task = current_task->next_task;
			printk("find_task: Going from task with task_id: %d to next task\n", (int)current_task->task_id);
			if (start_task == current_task) {
				printk("Loop found, exiting...");
				return NULL;
			}
		}
	}
	else {
		printk("find_task: invalid container_id\n");
	}
	printk("find_task: task not found with task_id: %d\n", task_id);
	return NULL;
}

struct task_object *find_previous_task(struct container_list *container, int task_id) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering find_previous_task with input: task_id %d\n", task_id);
	#endif

	if (container != NULL) {

		struct task_object *current_task = container->tasks;

		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering while loop found in find_previous_task\n");
		#endif
		while (current_task->next_task != NULL) {

			if (current_task->next_task->task_id == task_id) {
				return current_task;
			}
			current_task = current_task->next_task;
		}
	}
	else {
		printk("find_task: invalid container_id\n");
	}
	printk("find_task: no previous task found with task_id: %d\n", task_id);
	return NULL;
}

struct container_list *find_previous_container(struct container_list *container) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk("Entering find_previous_container with input: container->container_id %d\n", container->container_id);
	#endif

	if (container != NULL) {

		struct container_list *current_container = container_head;

		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk("Entering while loop found in find_previous_container\n");
		#endif
		while (current_container->next_container != NULL) {

			if (current_container->next_container->container_id == container->container_id) {
				return current_container;
			}
			current_container = current_container->next_container;
		}
	}
	else {
		printk("find_previous_container: invalid container_id\n");
	}
	printk("find_previous_container: no previous container found with container_id: %d\n", container->container_id);
	return NULL;
}

struct task_object *add_task(struct container_list *container, struct task_struct *current_task, __u64 task_id) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering add_task with input: task_id %d\n", task_id);
	#endif

    struct task_object *task;
	struct task_object *head_task;
    struct task_object *new_task = kmalloc(sizeof(struct task_object), GFP_KERNEL);
    
    new_task->current_task = current_task;
    new_task->next_task = NULL;
    new_task->task_id = task_id;

	mutex_lock(container->container_lock);
	head_task = container->tasks;
    
    if (head_task == NULL) { 
		// If the container is new, place new task at the head
        head_task = new_task;
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutex_unlock found in add_task with new container\n");
		#endif
        mutex_unlock(container->container_lock);
        
        return new_task;
    }
    else {
    	task = head_task;
    	
    	if (task->next_task == NULL) {
			
    		task->next_task = new_task;
			
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering mutex_unlock found in add_task with one task in container\n");
			#endif
    		mutex_unlock(container->container_lock);
    		
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering set_current_state found in add_task with one task in container\n");
			#endif
    		set_current_state(TASK_INTERRUPTIBLE);
			
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering schedule found in add_task with one task in container\n");
			#endif
    		schedule();
    		
    		return new_task;
    	}
    	else {
    		
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering while loop found in add_task\n");
			#endif
    		while (task->next_task != NULL) {
				#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
					printk(KERN_DEBUG "Iterating from task id: %d found in add_task\n", task->task_id);
					printk(KERN_DEBUG "Iterating from pointer: %p to pointer: %p\n", task, task->next_task);
				#endif
    			task = task->next_task;
    		}
    		// end of task list
			
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering mutex_unlock found in add_task with multiple tasks in container\n");
			#endif
    		mutex_unlock(container->container_lock);
    		
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering set_current_state found in add_task with multiple tasks in container\n");
			#endif
    		set_current_state(TASK_INTERRUPTIBLE);
			
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering schedule found in add_task with multiple tasks in container\n");
			#endif
    		schedule();
    		
    		return head_task;
    	}
    }
}

int free_container(__u64 container_id) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering free_container with input: cid %d\n", (int)container_id);
	#endif

	struct container_list *container = NULL;
	struct container_list *prev_container = NULL;
	struct container_list *holding_container = NULL;

	container = find_container(container_id);

	if (container != NULL) {

		if (container->tasks != NULL) {
			printk("free_container: Container still has non-null tasks, exiting\n");
			return -1;
		}

		if (container_head->container_id == container->container_id) {
			if (container_head->next_container != NULL) {
				mutex_lock(container_head->next_container->container_lock);
				holding_container = container_head->next_container;
				mutex_unlock(container_head->next_container->container_lock);
				container_head = holding_container;
			} else {
				container_head = NULL;
			}
		} else {
			prev_container = find_previous_container(container);
			mutex_lock(prev_container->container_lock);
			if(container->next_container != NULL) {
				prev_container->next_container = container->next_container;
			} else {
				prev_container->next_container = NULL;
			}
			mutex_unlock(prev_container->container_lock);
		}
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering kfree found in free_container\n");
		#endif
		kfree(container);
		container = NULL;
	}
	else {
		printk("free_container: Container does not exist\n");
	}
	return 0;
}

/**
 * Project-defined functions
 */
int resource_container_delete(struct resource_container_cmd __user *user_cmd) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_delete with input: %d\n", (int)current->pid);
	#endif

	printk("entering resource_container_delete\n");
	
	struct resource_container_cmd user_space_cmd;
	struct container_list *current_container = NULL;

	struct task_object *task = NULL;
	struct task_object *previous_task = NULL;
	
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_lock found in resource_container_delete\n");
	#endif
	mutex_lock(&ioctl_lock);
	
	// cmd = kmalloc(sizeof(struct resource_container_cmd), GFP_KERNEL);
	
	if(copy_from_user(&user_space_cmd, user_cmd, sizeof(struct resource_container_cmd))) {
		printk("resource_container_delete: Copying command failed\n");
		return -1;
	}

	current_container = VM_find_container(current);

	if (current_container != NULL) {

		mutex_lock(current_container->container_lock);
		task = current_container->tasks;
			
		if (task == NULL) {
			
			printk("No tasks, removing container\n");
			mutex_unlock(current_container->container_lock);
			free_container(current_container->container_id);

			return 0;
		}
		else {

			task = find_task(current_container, current->pid);
			previous_task = find_previous_task(current_container, task->task_id);

			if (task != NULL)  {
				#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
					printk(KERN_DEBUG "Found task in resource_container_delete\n");
				#endif
				if (previous_task == NULL) {
					// we are at the head task
					#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
						printk(KERN_DEBUG "At head task in resource_container_delete\n");
					#endif
					if (task->next_task != NULL) {
						current_container->tasks = task->next_task;
					} else {
						current_container->tasks = NULL;
					}
				}
				else {
					if (task->next_task != NULL) {
						// link previous and next tasks before deleting
						previous_task->next_task = task->next_task;
					}
				}
				#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
					printk(KERN_DEBUG "Entering kfree found in resource_container_delete to delete a task\n");
				#endif
				kfree(task);
				task = NULL;

				if (current_container->tasks == NULL) {
					printk("No remaining tasks, container removed\n");
					free_container(current_container->container_id);
				}
				else {
					#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
						printk(KERN_DEBUG "Entering wake_up_process found in resource_container_delete to wake up next process after deleting a task\n");
					#endif
					wake_up_process((current_container->tasks)->current_task);
				}
			}
		}
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_delete after deleting a task\n");
		#endif
		mutex_unlock(current_container->container_lock);
	}
	else {
		printk("Container is null\n");
	}
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_delete\n");
	#endif
	mutex_unlock(&ioctl_lock);

	printk("exiting resource_container_delete\n");
	return 0;
}

int resource_container_create(struct resource_container_cmd __user *user_cmd) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_create with input: %d\n", (int)user_cmd->cid);
	#endif

	printk("entering resource_container_create with input: %d\n", (int)user_cmd->cid);
	struct container_list *current_container = NULL;
	struct container_list *holding_container = NULL;
	struct task_object *created_task = NULL;
	struct resource_container_cmd user_space_cmd;
		
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_lock found in resource_container_create\n");
	#endif
	mutex_lock(&ioctl_lock);
	// user_space_cmd = kmalloc(sizeof(struct resource_container_cmd), GFP_KERNEL);
	if(copy_from_user(&user_space_cmd, user_cmd, sizeof(struct resource_container_cmd))) {
		printk("resource_container_create: Copying command failed\n");
		return -1;
	}

	current_container = find_container(user_space_cmd.cid);
	
	if (current_container != NULL) {

		printk(KERN_INFO "Container already exists, adding task: %d at %p\n", (int)current->pid, current);
		created_task = add_task(current_container, current, current->pid);
	}
	else {
	
		printk("Creating new resource container\n");
		struct container_list *new_container;
		
		new_container = kmalloc(sizeof(struct container_list), GFP_KERNEL);
		new_container->container_lock = kmalloc(sizeof(struct mutex), GFP_KERNEL);
		
		new_container->container_id = user_space_cmd.cid;
		new_container->next_container = NULL;
		new_container->tasks = NULL;
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutux_init found in resource_container_create while creating a container\n");
		#endif
		mutex_init(new_container->container_lock);
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering add_task found in resource_container_create while creating a container\n");
			printk(KERN_INFO "Creating a container, adding task: %d at %p\n", (int)current->pid, current);
		#endif
		created_task = add_task(new_container, current, current->pid);
		new_container->tasks = created_task;
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Handling creation of new container\n");
		#endif
		if (container_head == NULL) {
		
			container_head = new_container;
			current_container = container_head;
		}
		else {
			current_container = container_head;
			mutex_lock(current_container->container_lock);

			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering while loop found in resource_container_create after creating a container\n");
			#endif
			while (current_container->next_container != NULL) {
				mutex_lock(current_container->next_container->container_lock);
				holding_container = current_container->next_container;
				mutex_unlock(current_container->container_lock);
				current_container = holding_container;
				mutex_lock(current_container->container_lock);
			}
			current_container->next_container = new_container;
			mutex_unlock(current_container->container_lock);
			current_container = new_container;
		}
		if (find_container(new_container->container_id) != NULL) {
			printk("Container successfully created\n");
		}
		mutex_unlock(new_container->container_lock);
	}
	
	if (find_task(current_container, created_task->task_id) != NULL) {
		printk("Task created successfully\n");
	}
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_create\n");
	#endif
	mutex_unlock(&ioctl_lock);
	printk("exiting resource_container_create\n");

	return 0;
}

/**
 * This function uses a round-robin scheduling algorithm. When called, it switches
 * from current_task to current_task->next_task. If current_task->next_task is
 * null, it moves to the head of the task list, current_container->tasks.
 */
int resource_container_switch(struct resource_container_cmd __user *user_cmd) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_switch with input: %d\n", (int)user_cmd->cid);
	#endif

	printk("entering resource_container_switch\n");

	struct container_list *current_container = NULL;
	struct resource_container_cmd user_space_cmd;
		
	// user_space_cmd = kmalloc(sizeof(struct resource_container_cmd), GFP_KERNEL);
	copy_from_user(&user_space_cmd, user_cmd, sizeof(struct resource_container_cmd));

	current_container = find_container(user_space_cmd.cid);

	mutex_lock(current_container->container_lock);

	if (current_container != NULL) {

		struct task_object *current_task = find_task(current_container, current->pid);
		struct task_object *new_task = NULL;

		if (current_task->next_task != NULL) {
			// schedule next task
			new_task = current_task->next_task;
		}
		else {
			// schedule container head task
			new_task = current_container->tasks;
		}
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering wake_up_process found in resource_container_switch after getting next task to wake_up\n");
		#endif
		wake_up_process(new_task->current_task);
		
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_switch after getting next task to wake_up\n");
		#endif
		mutex_unlock(current_container->container_lock);
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering set_current_state found in resource_container_switch after getting next task to wake_up\n");
		#endif
		set_current_state(TASK_RUNNING);
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering schedule found in resource_container_switch after getting next task to wake_up\n");
		#endif
		schedule();

		printk("new_task scheduled\n");

	} else {
		printk("Container not found, cannot switch task\n");
		mutex_unlock(current_container->container_lock);

		return -1;
	}
	printk("exiting resource_container_switch\n");
	return 0;
}

/**
 * Allocates memory in kernal space for sharing with tasks in the same container and 
 * maps the virtual address to the physical address.
 */
int resource_container_mmap(struct file *filp, struct vm_area_struct *vma) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_mmap with input: pid %ld\n", (int)current->pid);
	#endif

    int ret;
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_lock found in resource_container_mmap\n");
	#endif
	mutex_lock(&ioctl_lock);
	struct container_list *container = VM_find_container(current);		
	struct share_object *o = find_object((__u64)vma->vm_pgoff, container);
	
	if(o == NULL) {
		struct share_object *new_shared_object; 
		struct share_object *current_object = container->objects;
		struct share_object *prev = NULL;
		unsigned long oid = vma->vm_pgoff;
		int flag = 0;
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering while loop found in resource_container_mmap if object is not found\n");
		#endif
		while(current_object != NULL) {
			prev = current_object;
			current_object = current_object->next;
		}
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering kcalloc found in resource_container_mmap if object is not found\n");
		#endif
		char *allocated_area = (char*) kcalloc(1, (vma->vm_end - vma->vm_start)*sizeof(char), GFP_KERNEL);
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering virt_to_phys found in resource_container_mmap if object is not found\n");
		#endif
		unsigned long pfn = virt_to_phys((void*)allocated_area) >> PAGE_SHIFT;
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering remap_pfn_range found in resource_container_mmap if object is not found\n");
		#endif
		remap_pfn_range(vma, vma->vm_start, pfn, vma->vm_end - vma->vm_start, vma->vm_page_prot);
		
		new_shared_object = (struct share_object*) kmalloc(sizeof(struct share_object), GFP_KERNEL);
		new_shared_object->pfn = pfn;
		new_shared_object->address = allocated_area;
		new_shared_object->oid = (__u64)vma->vm_pgoff;
		new_shared_object->next = NULL;

		if(prev == NULL) {
			container->objects = new_shared_object;
		} else {
			prev->next = new_shared_object;
		}
	} else if(o->oid == (__u64)vma->vm_pgoff) {
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering remap_pfn_range found in resource_container_mmap if object is found\n");
		#endif
		remap_pfn_range(vma, vma->vm_start, o->pfn, vma->vm_end - vma->vm_start, vma->vm_page_prot);
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_mmap if object is found\n");
		#endif
		mutex_unlock(&ioctl_lock);
		ret = 0;
		return ret;
	}
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_mmap at end of mmap\n");
	#endif
	mutex_unlock(&ioctl_lock);
	ret = 0;
    return ret;
}

/**
 * lock the container that is register by the current task.
 */
int resource_container_lock(struct resource_container_cmd __user *user_cmd) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_lock with input: oid %d\n", (int)user_cmd->oid);
	#endif
	struct resource_container_cmd user_space_cmd;
		
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_lock found in resource_container_lock\n");
	#endif
	mutex_lock(&ioctl_lock);
	
	// user_space_cmd = kmalloc(sizeof(struct resource_container_cmd), GFP_KERNEL);
    if(copy_from_user(&user_space_cmd, user_cmd, sizeof(struct resource_container_cmd))) {
		printk("resource_container_lock: Copying command failed\n");
		mutex_unlock(&ioctl_lock);
		return -1;
	}
	
	struct container_list *container = VM_find_container(current);
	__u64 oid = user_space_cmd.oid;
	struct lock_object *oid_lock = find_lock(oid, container);
	
	if(oid_lock == NULL) {
		
		struct lock_object *new_lock_object = NULL;
		struct lock_object *current_lock = container->locks;
		struct lock_object *prev = NULL;
		
		while(current_lock != NULL) {
			prev = current_lock;
			current_lock = current_lock->next;
		}
		
		new_lock_object = (struct lock_object*) kmalloc(sizeof(struct lock_object), GFP_KERNEL);
		new_lock_object->oid = oid;
		
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutex_init found in resource_container_lock if lock does not exist\n");
		#endif
		mutex_init(&new_lock_object->mutex_lock);
		new_lock_object->next = NULL;
		
		if(prev == NULL) {
			container->locks = new_lock_object;
		} else {
			prev->next = new_lock_object;
		}
		oid_lock = new_lock_object;
	}
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_lock for task lock found in resource_container_lock\n");
	#endif
	mutex_lock(&oid_lock->mutex_lock);
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_lock\n");
	#endif
	mutex_unlock(&ioctl_lock);
    return 0;
}

/**
 * unlock the container that is register by the current task.
 */
int resource_container_unlock(struct resource_container_cmd __user *user_cmd) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_unlock with input: oid %d cid %d\n", (int)user_cmd->oid, (int)user_cmd->cid);
	#endif
	mutex_lock(&ioctl_lock);
	
    struct resource_container_cmd user_space_cmd;
	//user_space_cmd = kmalloc(sizeof(struct resource_container_cmd), GFP_KERNEL);
    if(copy_from_user(&user_space_cmd, user_cmd, sizeof(struct resource_container_cmd))) {
		printk("resource_container_unlock: Copying command failed\n");
		return -1;
	}
	
	struct container_list *container = VM_find_container(current);
	__u64 oid = user_space_cmd.oid;
	struct lock_object *current_lock = find_lock(oid, container);
	
	if (current_lock != NULL) {
		#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
			printk(KERN_DEBUG "Entering mutex_unlock for task lock found in resource_container_unlock\n");
		#endif
		mutex_unlock(&current_lock->mutex_lock);
	} else {
		printk("lock could not be found with id: %d\n", (int)oid);
	}
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_unlock\n");
	#endif
	mutex_unlock(&ioctl_lock);
    return 0;
}

/**
 * clean the content of the object in the container that is register by the current task.
 */
int resource_container_free(struct resource_container_cmd __user *user_cmd) {
	#if DEBUG_LEVEL >= DEBUG_LEVEL_FUNCTION 
		printk(KERN_DEBUG "Entering resource_container_free with input: oid %d cid %d\n", (int)user_cmd->oid, (int)user_cmd->cid);
	#endif
	
    struct resource_container_cmd user_space_cmd;
	// user_space_cmd = kmalloc(sizeof(struct resource_container_cmd), GFP_KERNEL);
	
    if(copy_from_user(&user_space_cmd, user_cmd, sizeof(struct resource_container_cmd))) {
		printk("resource_container_unlock: Copying command failed\n");
		return -1;
	}
	
	__u64 object_id = user_space_cmd.oid;

	struct container_list *current_container = VM_find_container(current);
	struct container_list *prev_container = NULL;

	struct share_object *current_object = NULL;
	struct share_object *prev_object = NULL;
	struct share_object *object_head = current_container->objects;
	
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering while loop found in resource_container_free\n");
	#endif
	mutex_lock(current_container->container_lock);
	while(object_head != NULL) {
		
		if(object_head->oid == object_id) {
			current_object = object_head;
			if(prev_object == NULL) {
				current_container->objects = object_head->next;
			} else {
				prev_object->next = object_head->next;
			}
			
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering kfree found in resource_container_free for object address\n");
			#endif
			kfree(current_object->address);
			current_object->address = NULL;
			
			#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
				printk(KERN_DEBUG "Entering kfree found in resource_container_free for object struct\n");
			#endif
			kfree(current_object);
			current_object = NULL;
			break; // @TODO: Make this it's own function so I do not have to use break
		}
		prev_object = object_head;
		object_head = object_head->next;
	}
	#if DEBUG_LEVEL >= DEBUG_LEVEL_GRANULAR 
		printk(KERN_DEBUG "Entering mutex_unlock found in resource_container_free\n");
	#endif
	mutex_unlock(current_container->container_lock);
    return 0;
}

/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int resource_container_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
    case RCONTAINER_IOCTL_CSWITCH:
        return resource_container_switch((void __user *) arg);
    case RCONTAINER_IOCTL_CREATE:
        return resource_container_create((void __user *) arg);
    case RCONTAINER_IOCTL_DELETE:
        return resource_container_delete((void __user *) arg);
    case RCONTAINER_IOCTL_LOCK:
        return resource_container_lock((void __user *)arg);
    case RCONTAINER_IOCTL_UNLOCK:
        return resource_container_unlock((void __user *)arg);
    case RCONTAINER_IOCTL_FREE:
        return resource_container_free((void __user *)arg);
    default:
        return -ENOTTY;
    }
}

/*
## Your tasks

1. Implementing the resource_container kernel module: it needs the following features:

- create: you will need to support create operation that creates a container if the corresponding 
cid hasn't been assigned yet, and assign the task to the container. These create requests are 
invoked by the user-space library using ioctl interface. The ioctl system call will be redirected
 to resource_container_ioctl function located in src/ioctl.c

- delete: you will need to support delete operation that removes tasks from the container. If 
there is no task in the container, the container should be destroyed as well. These delete 
requests are invoked by the user-space library using ioctl interface. The ioctl system call will 
be redirected to resource_container_ioctl function located in src/ioctl.c

- switch: you will need to support Linux process scheduling mechanism to switch tasks between threads.

- lock/unlock: you will need to support locking and unlocking that guarantees only one process can 
access an object at the same time. These lock/unlock functions are invoked by the user-space library 
using ioctl interface. The ioctl system call will be redirected to resource_container_ioctl function 
located in src/ioctl.

- mmap: you will need to support mmap, the interface that user-space library uses to request the 
mapping of kernel space memory into the user-space memory. The kernel module takes an offset from 
the user-space library and allocates the requested size associated with that offset. You may consider 
that offset as an object id. If an object associated with an offset was already created/requested 
since the kernel module is loaded, the mmap request should assign the address of the previously 
allocated object to the mmap request. The kernel module interface will call 
resource_container_mmap() in src/core.c to request an mmap operation. One of the parameters for 
the resource_container_mmap() is "struct vm_area_struct *vma". This data structure contains page 
offset, starting virtual address, and etc, those you will need to allocate memory space. You also 
need to fill 0s in the allocated heap space.

- free: you will need to support delete operation that removes an object from resource_container. 
These delete requests are invoked by the user-space library using ioctl interface. The ioctl system 
call will be redirected to resource_container_ioctl function located in src/ioctl.c

*/
