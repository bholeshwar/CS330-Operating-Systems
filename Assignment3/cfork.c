#include <cfork.h>
#include <page.h>
#include <mmap.h>



/* You need to implement cfork_copy_mm which will be called from do_cfork in entry.c. Don't remove copy_os_pts()*/
void cfork_copy_mm(struct exec_context *child, struct exec_context *parent ){
	// This code has been copied from copy_mm and the DATA sgment has been changed
	void *os_addr;
	u64 vaddr;
	struct mm_segment *seg;

	child->pgd = os_pfn_alloc(OS_PT_REG);

	os_addr = osmap(child->pgd);
	bzero((char *)os_addr, PAGE_SIZE);

	//CODE segment
	seg = &parent->mms[MM_SEG_CODE];
	for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
	  u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
	  if(parent_pte)
	       install_ptable((u64) os_addr, seg, vaddr, (*parent_pte & FLAG_MASK) >> PAGE_SHIFT);   
	} 
   
	//RODATA segment
	seg = &parent->mms[MM_SEG_RODATA];
	for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
	  u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
	  if(parent_pte)
	       install_ptable((u64)os_addr, seg, vaddr, (*parent_pte & FLAG_MASK) >> PAGE_SHIFT);   
	} 
   
	//DATA segment
	seg = &parent->mms[MM_SEG_DATA];
	for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
	  u64 *parent_pte =  get_user_pte(parent, vaddr, 0);

	  if(parent_pte){
	  		*parent_pte = *parent_pte & (~0x2);

	        u64 pfn = map_physical_page((u64) os_addr, vaddr, MM_RD, (*parent_pte & FLAG_MASK) >> PAGE_SHIFT);                     
	        asm volatile ("invlpg (%0);" 
	                                :: "r"(vaddr) 
	                                : "memory"); // TLB Flush

	        struct pfn_info* phy_page = get_pfn_info(pfn);
	        increment_pfn_info_refcount(phy_page);
	  }
	} 
  
	//STACK segment
	seg = &parent->mms[MM_SEG_STACK];
	for(vaddr = seg->end - PAGE_SIZE; vaddr >= seg->next_free; vaddr -= PAGE_SIZE){
	  u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
	  
	 if(parent_pte){
	       u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
	       pfn = (u64)osmap(pfn);
	       memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
	  }
	}

	struct vm_area* parent_cur_vm = parent->vm_area;

	while( parent_cur_vm != NULL ) {
	    u64 vm_start = parent_cur_vm->vm_start;
	    u64 vm_end = parent_cur_vm->vm_end;

	    while( vm_start < vm_end){
	        u64 *parent_pte =  get_user_pte(parent, vm_start, 0);

	        if( parent_pte ) {
	        	*parent_pte = *parent_pte & (~0x2);

	            u64 pfn = map_physical_page((u64)os_addr, vm_start, MM_RD, (*parent_pte & FLAG_MASK) >> PAGE_SHIFT);
	            asm volatile ("invlpg (%0);" 
	                                :: "r"(vm_start) 
	                                : "memory"); // TLB Flush

	            struct pfn_info* phy_page = get_pfn_info(pfn);
	            increment_pfn_info_refcount(phy_page);
	        }

	        vm_start += 4096;
	    }

	    parent_cur_vm = parent_cur_vm->vm_next;
	}

    copy_os_pts(parent->pgd, child->pgd);
    return;
}

/* You need to implement cfork_copy_mm which will be called from do_vfork in entry.c.*/
void vfork_copy_mm(struct exec_context *child, struct exec_context *parent ){
	// TOOK HELP FROM A FRIEND
	void *os_addr;
	u64 vaddr;
	struct mm_segment *seg;
	struct mm_segment *child_seg;

	child->pgd = parent->pgd;
	os_addr = osmap(child->pgd);
	parent->state = WAITING;

	seg = &parent->mms[MM_SEG_STACK];
	u64 end1 = seg->end - PAGE_SIZE;
	vaddr = end1;

	while(vaddr >= (u64)seg->next_free){
		vaddr -= PAGE_SIZE;
	}

	u64 fsize = end1 - vaddr;

	for(vaddr = seg->end - PAGE_SIZE; vaddr >= seg->next_free; vaddr -= PAGE_SIZE) {
		u64 *parent_pte = get_user_pte(parent, vaddr, 0);
		if(parent_pte) {
			u64 pfn;
			u64 *old_pte = get_user_pte(parent, vaddr - fsize, 0);

			// if the page has already been allocated then no need to allocate again
			if(*old_pte) {
				pfn = (*old_pte & FLAG_MASK) >> PAGE_SHIFT;
			}
			else {
				pfn = install_ptable((u64)os_addr, seg, vaddr - fsize, 0);
			}
			pfn = (u64)osmap(pfn);
			memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE);
		}
	}

	child->regs.rbp -= fsize;
	child->regs.entry_rsp -= fsize;
	child_seg = &child -> mms[MM_SEG_STACK];
	child_seg->next_free = seg->next_free - fsize;
    return;
}

/*You need to implement handle_cow_fault which will be called from do_page_fault 
incase of a copy-on-write fault

* For valid acess. Map the physical page 
 * Return 1
 * 
 * For invalid access,
 * Return -1. 
*/

int handle_cow_fault(struct exec_context *current, u64 cr2){

    if( cr2 % 4096 != 0 ) {
        cr2 -= cr2%4096;
    }

    if( cr2 >= MMAP_AREA_START && cr2 < MMAP_AREA_END ) {
        struct vm_area* cur_vm = current->vm_area;

        while( cur_vm != NULL ) {
            if( cr2 >= cur_vm->vm_start && cr2 < cur_vm->vm_end ) {
                if( (cur_vm->access_flags & PROT_WRITE) == 0 ) {
                    asm volatile ("invlpg (%0);" 
                                            :: "r"(cr2) 
                                            : "memory"); // TLB Flush
                    return -1;
                }
                break;
            }

            cur_vm = cur_vm->vm_next;
        }

        u64 *parent_pte =  get_user_pte(current, cr2, 0);

        if(parent_pte) {
            u64 pfn = (*parent_pte & FLAG_MASK) >> PAGE_SHIFT;
            struct pfn_info* phy_page = get_pfn_info(pfn);
            int ref_count = get_pfn_info_refcount(phy_page);

            if( ref_count > 1 ) {
                decrement_pfn_info_refcount(phy_page);
                memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE);
                pfn = map_physical_page((u64)osmap(current->pgd), cr2, MM_WR, 0);
            }
            else {
            	memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE);
                pfn = map_physical_page((u64)osmap(current->pgd), cr2, MM_WR, pfn);
            }
        }
        else {
            return -1;
        }
    }

    else if( cr2 >= current->mms[MM_SEG_DATA].start && cr2 < current->mms[MM_SEG_DATA].end ) {

        if( current->mms[MM_SEG_DATA].access_flags & MM_WR ){
            u64 *parent_pte =  get_user_pte(current, cr2, 0);

            if(parent_pte) {
                u64 pfn = (*parent_pte & FLAG_MASK) >> PAGE_SHIFT;
                struct pfn_info* phy_page = get_pfn_info(pfn);
                int ref_count = get_pfn_info_refcount(phy_page);

                if( ref_count > 1 ) {
                    decrement_pfn_info_refcount(phy_page);
                    memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE);
                    pfn = map_physical_page((u64)osmap(current->pgd), cr2, MM_WR, 0);   
                }
                else{
                	memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE);
                    pfn = map_physical_page((u64)osmap(current->pgd), cr2, MM_WR, pfn);    
                }
            }
            else {
                return -1;    
            }

        }
        else {
            asm volatile ("invlpg (%0);" 
                                    :: "r"(cr2) 
                                    : "memory"); // TLB Flush
            return -1;
        }
    }

    asm volatile ("invlpg (%0);" 
                    		:: "r"(cr2) 
                   		    : "memory"); // TLB Flush
    
    return 1;
}

/* You need to handle any specific exit case for vfork here, called from do_exit*/
void vfork_exit_handle(struct exec_context *ctx){
	struct exec_context *parent;
	parent = get_ctx_by_pid(ctx->ppid);

	if( parent != NULL && (parent->pgd == ctx->pgd) ) {
		parent->state = READY;
		parent->vm_area = ctx->vm_area;
		parent->mms[MM_SEG_DATA].next_free = ctx->mms[MM_SEG_DATA].next_free;
	}
	
	return;
}