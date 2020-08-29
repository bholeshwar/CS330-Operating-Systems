#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<common.h>

/* XXX NOTE XXX  
       Do not declare any static/global variables. Answers deviating from this 
       requirement will not be graded.
*/
void init_rwlock(rwlock_t *lock)
{
   /*Your code for lock initialization*/
	lock->value = 0x1000000000000;
}

void write_lock(rwlock_t *lock)
{
	while( atomic_add(&lock->value, -0x1000000000000) ) {
		atomic_add(&lock->value, 0x1000000000000);
	}
   /*Your code to acquire write lock*/
}

void write_unlock(rwlock_t *lock)
{
	atomic_add(&lock->value, 0x1000000000000);
   /*Your code to release the write lock*/
}


void read_lock(rwlock_t *lock)
{
	while( atomic_add(&lock->value, -1) == -1 ) {
		atomic_add(&lock->value, 1);
	}
   /*Your code to acquire read lock*/
}

void read_unlock(rwlock_t *lock)
{
	atomic_add(&lock->value, 1);
   /*Your code to release the read lock*/
}
