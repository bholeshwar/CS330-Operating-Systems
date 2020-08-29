#include<types.h>
#include<mmap.h>

void map(unsigned long base, u64 addr, int access_flags, int length){
    u32 prot = 0;

    if( access_flags & PROT_WRITE ) {
        prot = (u32) PROT_READ|PROT_WRITE;
    }
    else {
        prot = (u32) PROT_READ;
    }

    u64 cur_addr = addr;

    while( cur_addr < addr + length ) { 
        u32 upfn = map_physical_page((u64) osmap(base), cur_addr, prot, (u32) 0);

        asm volatile ("invlpg (%0);" 
                        :: "r"(cur_addr) 
                        : "memory"); // TLB Flush
        
        cur_addr += 4096;                
    }

    return;
}

void unmap(struct exec_context *current, u64 addr , int length){
    u64 cur_addr = addr;

    while( cur_addr < addr + length ) {
        do_unmap_user(current, cur_addr);
        cur_addr += 4096;
    }

    return;
}


void update_map(struct exec_context* current, u64 addr, int access_flags, int length){
    u32 prot = 0;
    u64 cur_addr = addr;

    if( access_flags & PROT_WRITE ) {
        prot = (u32) PROT_READ | PROT_WRITE;
    }
    else {
        prot = (u32) PROT_READ;
    }

    while( cur_addr < addr + length ){ 
        u64* pte_addr = get_user_pte(current, cur_addr, 0);

        if( pte_addr ) {
            u32 upfn = (*pte_addr >> PAGE_SHIFT); 
            upfn = map_physical_page((u64)osmap(current->pgd), cur_addr, prot, (u32)0);

            asm volatile ("invlpg (%0);" 
                            :: "r"(cur_addr) 
                            : "memory"); // TLB Flush
        }

        cur_addr += 4096;                
    } 

    return;
}


/**
 * Function will invoked whenever there is page fault. (Lazy allocation)
 * 
 * For valid acess. Map the physical page 
 * Return 1
 * 
 * For invalid access,
 * Return -1. 
 */
int vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{

	struct vm_area *cur_vm;
	cur_vm = current->vm_area;

	int valid = 0;

	while( cur_vm != NULL ) {
		if( addr >= cur_vm->vm_start && addr < cur_vm->vm_end ) {
			if( (error_code == 6) && (cur_vm->access_flags & PROT_WRITE) != 0 ){
                valid = 1;
            }
            if((error_code == 4)){
                valid = 1;
            }
            break;
		}
		cur_vm = cur_vm->vm_next;
	}

	// invalid access: write fault to read-only vm_area
	if( !valid ) {
		asm volatile ("invlpg (%0);" 
                        :: "r"(addr) 
                        : "memory"); // TLB Flush
        return -1;
	}

	u32 prot;

	if( cur_vm->access_flags & PROT_WRITE ) {
		prot = (u32) PROT_WRITE | PROT_READ;
	}
	else {
		prot = (u32) PROT_READ;
	}

	u32 upfn;
	upfn = map_physical_page((u64) osmap(current->pgd), addr, prot, (u32) 0);

	asm volatile ("invlpg (%0);" 
                        :: "r"(addr) 
                        : "memory"); // TLB Flush

	return 1;

}


/**
 * mprotect System call Implementation.
 */
int vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
	struct vm_area *cur_vm;
	cur_vm = current->vm_area;

	u64 temp_addr = addr;
	int temp_length = length;

	while( cur_vm != NULL ) {
		if( temp_addr >= cur_vm->vm_start && temp_addr < cur_vm->vm_end ) {
			if( temp_addr + temp_length <= cur_vm->vm_end ) {
				break;
			}
			else {
				int temp1 = temp_addr;
				temp_addr = cur_vm->vm_end;
				temp_length -= temp_addr - temp1;
				continue;
			}
		}
		cur_vm = cur_vm->vm_next;
	}

	if( cur_vm == NULL ) return -1;
	cur_vm = current->vm_area;

	if( length % 4096 != 0 ) {
		length += 4096 - length%4096;
	}

	int old_length = length;

	int vm_count = 0;

	while( cur_vm != NULL ) {
		vm_count++;
		cur_vm = cur_vm->vm_next;
	}
	if( vm_count > 128 ) return -1;

	cur_vm = current->vm_area;

	if( addr < MMAP_AREA_START || addr + length > MMAP_AREA_END ) {
		return -1;
	}

	if( cur_vm == NULL ) {
		return -1;
	}

	struct vm_area *prev_vm = NULL;
	int ret_val = -1;
			
	while( cur_vm != NULL && addr >= cur_vm->vm_end ) {
		prev_vm = cur_vm;
		cur_vm = cur_vm->vm_next;
	}

	if( cur_vm == NULL ) {
		return -1;
	}

	if( addr < cur_vm->vm_start ) {
		return -1;
	}

	if( addr > cur_vm->vm_start ) {
		if( cur_vm->access_flags == prot && addr + length <= cur_vm->vm_end ) {
			ret_val = 0;
		}

		// region to be changed is inside existing vm_area
		else if( addr + length < cur_vm->vm_end ) {
			if( vm_count >= 127 ) return -1;

			struct vm_area *new_vm2 = alloc_vm_area();
			new_vm2->vm_start = addr + length;
			new_vm2->vm_end = cur_vm->vm_end;
			new_vm2->access_flags = cur_vm->access_flags;
			new_vm2->vm_next = cur_vm->vm_next;

			struct vm_area *new_vm1 = alloc_vm_area();
			new_vm1->vm_start = addr;
			new_vm1->vm_end = addr + length;
			new_vm1->access_flags = prot;
			new_vm1->vm_next = new_vm2;

			cur_vm->vm_end = addr;
			cur_vm->vm_next = new_vm1;

			ret_val = 0;
		}

		//
		else if( addr + length == cur_vm->vm_end ) {
			// region to be changed is at the end of an existing vm_area
			if( cur_vm->vm_next == NULL || cur_vm->vm_next->vm_start != cur_vm->vm_end || cur_vm->vm_next->access_flags != prot ) {
				if( vm_count >= 128 ) return -1;
				struct vm_area *new_vm = alloc_vm_area();
				new_vm->vm_start = addr;
				new_vm->vm_end = addr + length;
				new_vm->access_flags = prot;
				new_vm->vm_next = cur_vm->vm_next;

				cur_vm->vm_end = addr;
				cur_vm->vm_next = new_vm;
				ret_val = 0;
			}

			// region to be changed is at the end of existing vm_area and new vm_area has same rights
			else if( cur_vm->vm_end == cur_vm->vm_next->vm_start && cur_vm->vm_next->access_flags == prot ) {
				cur_vm->vm_end = addr;
				cur_vm->vm_next->vm_start = addr;
				ret_val = 0;
			}
		}

		else if( addr + length > cur_vm->vm_end ) {
			if( cur_vm->vm_next == NULL ) {
				return -1;
			}
			else {
				if( cur_vm->access_flags == prot ) {
					length -= cur_vm->vm_end - addr;
					return vm_area_mprotect(current, cur_vm->vm_end, length, prot);
				}
				else {
					if( vm_count >= 128 ) return -1;
					struct vm_area *new_vm = alloc_vm_area();
					new_vm->vm_start = addr;
					new_vm->vm_end = cur_vm->vm_end;
					new_vm->access_flags = prot;
					new_vm->vm_next = cur_vm->vm_next;

					cur_vm->vm_end = addr;
					cur_vm->vm_next = new_vm;
					length -= new_vm->vm_end - addr;
					return vm_area_mprotect(current, new_vm->vm_end, length, prot);
				}				

			}
		}
	}

	// region to be changed is at the front of existing vm_area
	else if( addr == cur_vm->vm_start ) {
		if( addr + length < cur_vm->vm_end ) {
			if( cur_vm->access_flags == prot ) {
				ret_val = 0;
			}
			else if( prev_vm != NULL && prev_vm->vm_end == cur_vm->vm_start && prev_vm->access_flags == prot ) {
				prev_vm->vm_end = addr + length;
				cur_vm->vm_start = addr + length;
				ret_val = 0;
			}
			else {
				if( vm_count >= 128 ) return -1;
				struct vm_area *new_vm = alloc_vm_area();
				new_vm->vm_start = addr;
				new_vm->vm_end = addr + length;
				new_vm->access_flags = prot;
				new_vm->vm_next = cur_vm;

				if( prev_vm != NULL ) prev_vm->vm_next = new_vm;
				else current->vm_area = new_vm;
				cur_vm->vm_start = addr + length;

				ret_val = 0;
			}
		}

		else if( addr + length == cur_vm->vm_end ) {
			if( prev_vm != NULL && prev_vm->vm_end == cur_vm->vm_start && prev_vm->access_flags == prot ) {
				prev_vm->vm_end = cur_vm->vm_end;
				prev_vm->vm_next = cur_vm->vm_next;
				dealloc_vm_area(cur_vm);
				ret_val = 0;

				if( cur_vm->vm_next != NULL && cur_vm->vm_end == cur_vm->vm_next->vm_start && cur_vm->vm_next->access_flags == prot ) {
					struct vm_area *next_vm = prev_vm->vm_next;
					prev_vm->vm_end = prev_vm->vm_next->vm_end;
					prev_vm->vm_next = prev_vm->vm_next->vm_next;
					dealloc_vm_area(next_vm);
					ret_val = 0;
				}
			}
			else {
				cur_vm->access_flags = prot;
				ret_val = 0;

				if( cur_vm->vm_next != NULL && cur_vm->vm_end == cur_vm->vm_next->vm_start && cur_vm->vm_next->access_flags == prot ) {
					struct vm_area *next_vm = cur_vm->vm_next;
					cur_vm->vm_end = cur_vm->vm_next->vm_end;
					cur_vm->vm_next = cur_vm->vm_next->vm_next;
					dealloc_vm_area(next_vm);
					ret_val = 0;
				}
			}
		}

		else if( addr + length > cur_vm->vm_end ) {
			if( cur_vm->vm_next == NULL ) {
				return -1;
			}
			else {
				if( prev_vm != NULL && prev_vm->vm_end == cur_vm->vm_start && prev_vm->access_flags == prot ) {
					prev_vm->vm_end = cur_vm->vm_end;
					prev_vm->vm_next = cur_vm->vm_next;
					dealloc_vm_area(cur_vm);
					length -= prev_vm->vm_end - addr;
					return vm_area_mprotect(current, prev_vm->vm_end, length, prot);
				}
				else if( cur_vm->access_flags == prot ) {
					length -= cur_vm->vm_end - addr;
					return vm_area_mprotect(current, cur_vm->vm_end, length, prot);
				}
				else {
					cur_vm->access_flags = prot;

					length -= cur_vm->vm_end - addr;
					return vm_area_mprotect(current, cur_vm->vm_end, length, prot);
				}				

			}
		}
	}

	update_map(current, addr, prot, old_length);

	return ret_val;
}
/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
	struct vm_area *cur_vm;
	cur_vm = current->vm_area;

	if( length % 4096 != 0 ) {
		length += 4096 - length%4096;
	} 

	long ret_addr = -1;

	int vm_count = 0;

	while( cur_vm != NULL ) {
		vm_count++;
		cur_vm = cur_vm->vm_next;
	}
	if( vm_count > 128 ) return -1;

	cur_vm = current->vm_area;

	// without hint address
	if( addr == 0 ) {

		// no vm is assigned till now
		if( cur_vm == NULL ) {
			if( (MMAP_AREA_START + length) <= MMAP_AREA_END ) {
				if( vm_count >= 128 ) return -1;
				struct vm_area *new_vm = alloc_vm_area();
				new_vm->vm_start = MMAP_AREA_START;
				new_vm->vm_end = MMAP_AREA_START + length;
				new_vm->access_flags = prot;
				new_vm->vm_next = cur_vm;
				current->vm_area = new_vm;
				ret_addr = MMAP_AREA_START;
			}
		}

		// vm is created at the very start of mmap_area
		else if( cur_vm->vm_start > MMAP_AREA_START && (MMAP_AREA_START + length) <= cur_vm->vm_start ) {
			if( prot == cur_vm->access_flags && (MMAP_AREA_START + length) == cur_vm->vm_start) {
				cur_vm->vm_start = MMAP_AREA_START;
				ret_addr = MMAP_AREA_START;
			}
			else {
				if( vm_count >= 128 ) return -1;
				struct vm_area *new_vm = alloc_vm_area();
				new_vm->vm_start = MMAP_AREA_START;
				new_vm->vm_end = MMAP_AREA_START + length;
				new_vm->access_flags = prot;
				new_vm->vm_next = cur_vm;
				current->vm_area = new_vm;
				ret_addr = MMAP_AREA_START;
			}
		}

		// vm is not created at the very start of mmap_area
		else {
			struct vm_area *next_vm;

			while(1) {
				next_vm = cur_vm->vm_next;

				// if there is only 1 vm
				if( next_vm == NULL ) {
					if( (cur_vm->vm_end + length) <= MMAP_AREA_END ) {
						// merging with current_vm
						if( prot == cur_vm->access_flags ) {
							ret_addr = cur_vm->vm_end;
							cur_vm->vm_end += length;
							break;							
						}
						// creating a new_vm
						else {
							if( vm_count >= 128 ) return -1;
							struct vm_area *new_vm = alloc_vm_area();
							new_vm->vm_start = cur_vm->vm_end;
							new_vm->vm_end = cur_vm->vm_end + length;
							new_vm->access_flags = prot;
							new_vm->vm_next = next_vm;
							cur_vm->vm_next = new_vm;
							ret_addr = cur_vm->vm_end;
							break;
						}
					}
				}

				// if next_vm isn't NULL
				else {
					// the new_vm can be inserted just after cur_vm
					if( (cur_vm->vm_end + length) <= next_vm->vm_start ) {
						// 3 vms are merged
						if( (cur_vm->vm_end + length) == next_vm->vm_start && cur_vm->access_flags == prot && next_vm->access_flags == prot ) {
							ret_addr = cur_vm->vm_end + length;
							cur_vm->vm_end = next_vm->vm_end;
							cur_vm->vm_next = next_vm->vm_next;
							dealloc_vm_area(next_vm);
							break;
						}

						// cur_vm is merged with new_vm
						else if( cur_vm->access_flags == prot ) {
							ret_addr = cur_vm->vm_end;
							cur_vm->vm_end += length;
							break;
						}

						// new_vm is merged with next_vm
						else if( (cur_vm->vm_end + length) == next_vm->vm_start && next_vm->access_flags == prot ) {
							next_vm->vm_start = cur_vm->vm_end;
							ret_addr = cur_vm->vm_end;
							break;
						}

						// new_vm is created after cur_vm
						else {
							if( vm_count >= 128 ) return -1;
							struct vm_area *new_vm = alloc_vm_area();
							new_vm->vm_start = cur_vm->vm_end;
							new_vm->vm_end = cur_vm->vm_end + length;
							new_vm->access_flags = prot;
							new_vm->vm_next = next_vm;
							cur_vm->vm_next = new_vm;
							ret_addr = cur_vm->vm_end;
							break;
						}
					}

					else {
						cur_vm = cur_vm->vm_next;
						continue;
					}
				}
			}
		}
	}

	//with hint address
	else {

		if( addr < MMAP_AREA_START || addr > MMAP_AREA_END ) {
			return -1;
		}

		if( addr + length > MMAP_AREA_END ){
    		if(flags == MAP_FIXED) return -1;
    		else return vm_area_map(current, 0, length, prot, flags);
    	}

    	// if mmap_area has no vms
		if( cur_vm == NULL || addr + length <= cur_vm->vm_start ) {
			if( vm_count >= 128 ) return -1;
			struct vm_area *new_vm = alloc_vm_area();
			new_vm->vm_start = addr;
			new_vm->vm_end = addr + length;
			new_vm->access_flags = prot;
			new_vm->vm_next = cur_vm;
			current->vm_area = new_vm;
			ret_addr = addr;
		}

		else {
			struct vm_area *prev_vm = NULL;
			
			// cur_vm has end less than or equal to addr
			while( cur_vm != NULL && addr >= cur_vm->vm_end ) {
				prev_vm = cur_vm;
				cur_vm = cur_vm->vm_next;
			}

			if( cur_vm == NULL ) {
				if( vm_count >= 128 ) return -1;
				struct vm_area *new_vm = alloc_vm_area();
				new_vm->vm_start = addr;
				new_vm->vm_end = addr + length;
				new_vm->access_flags = prot;
				new_vm->vm_next = cur_vm;
				prev_vm->vm_next = new_vm;
				ret_addr = addr;
			}
			
			else if( ( addr >= cur_vm->vm_start && addr < cur_vm->vm_end ) || ( addr < cur_vm->vm_start && (addr+length) > cur_vm->vm_start ) ){
				if(flags & MAP_FIXED) return -1;

				if( cur_vm->vm_end + addr <= MMAP_AREA_END ) return vm_area_map(current, cur_vm->vm_end, length, prot, flags);
				else return vm_area_map(current, 0, length, prot, flags);
			}

			else if( addr < cur_vm->vm_start && (addr + length <= cur_vm->vm_start) ) {
				// 1-2-3 merge
				if( addr == prev_vm->vm_end && addr+length == cur_vm->vm_start && prot == prev_vm->access_flags && prot == cur_vm->access_flags ) {
					prev_vm->vm_end = cur_vm->vm_end;
					prev_vm->vm_next = cur_vm->vm_next;
					dealloc_vm_area(cur_vm);
					ret_addr = addr;
				}

				// 1-2 merge
				else if( addr == prev_vm->vm_end && prot == prev_vm->access_flags ) {
					prev_vm->vm_end = addr + length;
					ret_addr = addr;
				}

				// 2-3 merge
				else if( addr + length == cur_vm->vm_start && prot == cur_vm->access_flags ) {
					cur_vm->vm_start = addr;
					ret_addr = addr;
				}

				// no merge
				else {
					if( vm_count >= 128 ) return -1;
					struct vm_area *new_vm = alloc_vm_area();
					new_vm->vm_start = addr;
					new_vm->vm_end = addr + length;
					new_vm->access_flags = prot;
					new_vm->vm_next = cur_vm;
					prev_vm->vm_next = new_vm;
					ret_addr = addr;
				}
			}

		}
	}

	if( ret_addr == -1 ) return ret_addr;

	if( flags & MAP_POPULATE ) {
        map(current->pgd, ret_addr, prot, length);  
    }   

	return ret_addr;
}


/**
 * munmap system call implemenations
 */

int vm_area_unmap(struct exec_context *current, u64 addr, int length)
{

	struct vm_area *cur_vm;
	cur_vm = current->vm_area;
	struct vm_area *prev_vm = NULL;

	if( length % 4096 != 0 ) {
		length += 4096 - length%4096;
	} 

	int vm_count = 0;

	while( cur_vm != NULL ) {
		vm_count++;
		cur_vm = cur_vm->vm_next;
	}
	if( vm_count > 128 ) return -1;

	cur_vm = current->vm_area;

	int old_length = length;
	u64 cur_addr = addr;

	while( cur_vm != NULL ) {

		if( length <= 0 ) {
			break;
		}
		
		if( cur_addr > cur_vm->vm_start && cur_addr < cur_vm->vm_end ) {
			if( cur_addr + length < cur_vm->vm_end ) {
				if( vm_count >= 128 ) return -1;
				struct vm_area *new_vm = alloc_vm_area();
				new_vm->vm_start = cur_addr + length;
				new_vm->vm_end = cur_vm->vm_end;
				new_vm->access_flags = cur_vm->access_flags;
				new_vm->vm_next = cur_vm->vm_next;
				cur_vm->vm_end = cur_addr;
				cur_vm->vm_next = new_vm;
				break;
			}
			
			else {
				cur_vm->vm_end = cur_addr;

				length -= cur_vm->vm_end - cur_addr;
				cur_addr = cur_vm->vm_end;
				prev_vm = cur_vm;
				cur_vm = cur_vm->vm_next;
				continue;
			}
		}

		else if( cur_addr <= cur_vm->vm_start && (cur_addr + length < cur_vm->vm_end) ) {
			cur_vm->vm_start = cur_addr + length;
			break;
		}

		else if( cur_addr <= cur_vm->vm_start && (cur_addr + length >= cur_vm->vm_end) ){
			if( prev_vm != NULL ) {
				prev_vm->vm_next = cur_vm->vm_next;
				dealloc_vm_area(cur_vm);
				cur_vm = prev_vm->vm_next;
			}
			else {
				current->vm_area = cur_vm->vm_next;
				dealloc_vm_area(cur_vm);
				cur_vm = current->vm_area;
			}
			if( cur_vm == NULL ) break;
			length -= cur_vm->vm_start - cur_addr;
			cur_addr = cur_vm->vm_start;
			continue;
		}

		prev_vm = cur_vm;
		if( cur_vm != NULL ) cur_vm = cur_vm->vm_next;
	}

	unmap(current, addr, old_length);

	return 0;
}
