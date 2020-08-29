#include<types.h>
#include<context.h>
#include<file.h>
#include<lib.h>
#include<serial.h>
#include<entry.h>
#include<memory.h>
#include<fs.h>
#include<kbd.h>
#include<pipe.h>


/************************************************************************************/
/***************************Do Not Modify below Functions****************************/
/************************************************************************************/
void free_file_object(struct file *filep)
{
    if(filep)
    {
       os_page_free(OS_DS_REG ,filep);
       stats->file_objects--;
    }
}

struct file *alloc_file()
{
  
  struct file *file = (struct file *) os_page_alloc(OS_DS_REG); 
  file->fops = (struct fileops *) (file + sizeof(struct file)); 
  bzero((char *)file->fops, sizeof(struct fileops));
  stats->file_objects++;
  return file; 
}

static int do_read_kbd(struct file* filep, char * buff, u32 count)
{
  kbd_read(buff);
  return 1;
}

static int do_write_console(struct file* filep, char * buff, u32 count)
{
  struct exec_context *current = get_current_ctx();
  return do_write(current, (u64)buff, (u64)count);
}

struct file *create_standard_IO(int type)
{
  struct file *filep = alloc_file();
  filep->type = type;
  if(type == STDIN)
     filep->mode = O_READ;
  else
      filep->mode = O_WRITE;
  if(type == STDIN){
        filep->fops->read = do_read_kbd;
  }else{
        filep->fops->write = do_write_console;
  }
  filep->fops->close = generic_close;
  filep->ref_count = 1;
  return filep;
}

int open_standard_IO(struct exec_context *ctx, int type)
{
   int fd = type;
   struct file *filep = ctx->files[type];
   if(!filep){
        filep = create_standard_IO(type);
   }else{
         filep->ref_count++;
         fd = 3;
         while(ctx->files[fd])
             fd++; 
   }
   ctx->files[fd] = filep;
   return fd;
}
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/



void do_file_fork(struct exec_context *child)
{
   /*TODO the child fds are a copy of the parent. Adjust the refcount*/
  if( !child )
    return;

  int i;
  for(i=0; i<MAX_OPEN_FILES; i++) {
    if( child->files[i] ) {
      child->files[i]->ref_count++;
    }
  }
 
}

void do_file_exit(struct exec_context *ctx)
{
    /*TODO the process is exiting. Adjust the ref_count
     of files*/
  if( !ctx )
    return;

  int i;
  for(i=0; i<MAX_OPEN_FILES; i++) {
    if( ctx->files[i] ) {
      generic_close(ctx->files[i]);
    }
  }
}

long generic_close(struct file *filep)
{
  /** TODO Implementation of close (pipe, file) based on the type 
   * Adjust the ref_count, free file object
   * Incase of Error return valid Error code 
   */
  if( !filep ) {
    return -EINVAL;
  }
  filep->ref_count--;

  if( filep->ref_count == 0 ) {
    if( filep->type == PIPE) {
      if( (filep->mode == O_READ) && (filep->pipe->is_ropen) )
        filep->pipe->is_ropen = 0;
      if( (filep->mode == O_WRITE) && (filep->pipe->is_wopen) )
        filep->pipe->is_wopen = 0;

      if( !filep->pipe->is_wopen && !filep->pipe->is_ropen )
        free_pipe_info(filep->pipe);
    }

    free_file_object(filep);
  }

  return 0;

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}

static int do_read_regular(struct file *filep, char * buff, u32 count)
{
   /** TODO Implementation of File Read, 
    *  You should be reading the content from File using file system read function call and fill the buf
    *  Validate the permission, file existence, Max length etc
    *  Incase of Error return valid Error code 
    * */
  if( !filep || (buff==NULL) )
    return -EINVAL;

  if( (filep->mode & O_READ) != O_READ)
    return -EACCES;

  int offp = filep->offp;
  if( offp + count > filep->inode->file_size )
    count = filep->inode->file_size - offp;

  int bytes_read = flat_read(filep->inode, buff, count, &offp);
  filep->offp += bytes_read;
  return bytes_read;

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}


static int do_write_regular(struct file *filep, char * buff, u32 count)
{
    /** TODO Implementation of File write, 
    *   You should be writing the content from buff to File by using File system write function
    *   Validate the permission, file existence, Max length etc
    *   Incase of Error return valid Error code 
    * */
  if( !filep || (buff==NULL) )
    return -EINVAL;

  if( (filep->mode & O_WRITE) != O_WRITE)
    return -EACCES;

  int offp = filep->offp;
  if( offp + count > 4096 )
    return -EOTHERS;

  int bytes_written = flat_write(filep->inode, buff, count, &offp);
  filep->offp += bytes_written;
  return bytes_written;

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}

static long do_lseek_regular(struct file *filep, long offset, int whence)
{
    /** TODO Implementation of lseek 
    *   Set, Adjust the ofset based on the whence
    *   Incase of Error return valid Error code 
    * */
  if( !filep )
    return -EINVAL;

  u32 offp_old = filep->offp;

  if( whence == SEEK_SET) {
    if( offset < 0)
      return -EINVAL;

    filep->offp = offset;
  }
  else if( whence == SEEK_CUR ) {
    if( offset + filep->offp < 0 )
      return -EINVAL;

    filep->offp += offset;
  }
  else if( whence == SEEK_END ) {
    if( offset + filep->inode->file_size < 0 ) 
      return -EINVAL;

    filep->offp = filep->inode->file_size + offset;
  }
  else {
    return -EINVAL;
  }

  if( filep->offp > filep->inode->e_pos - filep->inode->s_pos) {
    filep->offp = offp_old;
    return -EINVAL;
  }
  else {
    return filep->offp;
  }

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}

extern int do_regular_file_open(struct exec_context *ctx, char* filename, u64 flags, u64 mode)
{ 
  /**  TODO Implementation of file open, 
    *  You should be creating file(use the alloc_file function to creat file), 
    *  To create or Get inode use File system function calls, 
    *  Handle mode and flags 
    *  Validate file existence, Max File count is 32, Max Size is 4KB, etc
    *  Incase of Error return valid Error code
    * */
  if( !ctx || !filename || !flags) 
    return -EINVAL;

  struct inode* file_inode;

  file_inode = lookup_inode(filename);

  int created = 0;

  if( file_inode == NULL && (flags & O_CREAT) == O_CREAT ) {
    created = 1;
    file_inode = create_inode(filename, mode);
  }

  if( !file_inode )
    return -EINVAL;

  if( file_inode->file_size > 4096)
    return -EOTHERS;

  // check compatibility of mode with flags... IMPLEMENTATION IS AS PER POSIX
  if( !created) {
    if( ( ( (file_inode->mode & O_WRITE) != O_WRITE ) && ( (flags & O_WRITE) == O_WRITE ) ) || ( ( (file_inode->mode & O_READ) != O_READ ) && ( (flags & O_READ) == O_READ ) ) )
      return -EACCES;
  }

  struct file *filep = alloc_file();
  if( !filep )
    return -ENOMEM; // Unable to allocate memory

  filep->type = REGULAR;
  if( created ) 
    filep->mode = file_inode->mode;
  else 
    filep->mode = flags ^ O_CREAT;
  filep->offp = 0;
  filep->ref_count = 1;
  filep->inode = file_inode;
  filep->fops->read = do_read_regular;
  filep->fops->write = do_write_regular;
  filep->fops->lseek = do_lseek_regular;
  filep->fops->close = generic_close;
  filep->pipe = NULL;

  int fd = 3;
  while(ctx->files[fd]) {
    fd++;
    if( fd >= MAX_OPEN_FILES )
      return -EOTHERS;
  }
    
  ctx->files[fd] = filep;
  return fd;

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}

int fd_dup(struct exec_context *current, int oldfd)
{
    /** TODO Implementation of dup 
    *  Read the man page of dup and implement accordingly 
    *  return the file descriptor,
    *  Incase of Error return valid Error code 
    * */
  if( oldfd >= MAX_OPEN_FILES || oldfd <  0 )
    return -EINVAL;

  if( !current || !current->files[oldfd] )
    return -EINVAL;

  struct file *filep1 = current->files[oldfd];

  int newfd = 0;
  while(current->files[newfd]) {
    newfd++;
    if( newfd >= MAX_OPEN_FILES )
      return -EOTHERS;
  }

  filep1->ref_count++;    
  current->files[newfd] = filep1;

  return newfd;

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}


int fd_dup2(struct exec_context *current, int oldfd, int newfd)
{
  /** TODO Implementation of the dup2 
    *  Read the man page of dup2 and implement accordingly 
    *  return the file descriptor,
    *  Incase of Error return valid Error code 
    * */
  if( oldfd >= MAX_OPEN_FILES || newfd >= MAX_OPEN_FILES || oldfd < 0 || newfd < 0 )
    return -EINVAL;

  if( !current || !current->files[oldfd] )
    return -EINVAL;

  struct file *filep1 = current->files[oldfd];
  filep1->ref_count++;

  if( current->files[newfd] )
    generic_close(current->files[newfd]);
  
  current->files[newfd] = filep1;

  return newfd;

  // int ret_fd = -EINVAL; 
  // return ret_fd;
}
