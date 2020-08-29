#include<pipe.h>
#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
/***********************************************************************
 * Use this function to allocate pipe info && Don't Modify below function
 ***********************************************************************/
struct pipe_info* alloc_pipe_info()
{
    struct pipe_info *pipe = (struct pipe_info*)os_page_alloc(OS_DS_REG);
    char* buffer = (char*) os_page_alloc(OS_DS_REG);
    pipe ->pipe_buff = buffer;
    return pipe;
}


void free_pipe_info(struct pipe_info *p_info)
{
    if(p_info)
    {
        os_page_free(OS_DS_REG ,p_info->pipe_buff);
        os_page_free(OS_DS_REG ,p_info);
    }
}
/*************************************************************************/
/*************************************************************************/


int pipe_read(struct file *filep, char *buff, u32 count)
{
    /**
    *  TODO:: Implementation of Pipe Read
    *  Read the contect from buff (pipe_info -> pipe_buff) and write to the buff(argument 2);
    *  Validate size of buff, the mode of pipe (pipe_info->mode),etc
    *  Incase of Error return valid Error code 
    */
    if( !filep || (buff==NULL) )
        return -EINVAL;

    if( !filep->pipe->is_ropen || filep->mode != O_READ )
        return -EACCES;

    if( count > filep->pipe->buffer_offset )
        count = filep->pipe->buffer_offset;

    int buf_ind = 0;
    while(buf_ind < count){
        buff[buf_ind] = filep->pipe->pipe_buff[buf_ind];
        buf_ind++;
    }

    int buf_ind2 = 0;
    while(buf_ind < filep->pipe->buffer_offset){
        filep->pipe->pipe_buff[buf_ind2] = filep->pipe->pipe_buff[buf_ind];
        buf_ind++;
        buf_ind2++;
    }
    filep->pipe->buffer_offset = buf_ind2;

    return count;

    // int ret_fd = -EINVAL; 
    // return ret_fd;
}


int pipe_write(struct file *filep, char *buff, u32 count)
{
    /**
    *  TODO:: Implementation of Pipe Read
    *  Write the contect from   the buff(argument 2);  and write to buff(pipe_info -> pipe_buff)
    *  Validate size of buff, the mode of pipe (pipe_info->mode),etc
    *  Incase of Error return valid Error code 
    */
    if( !filep || (buff==NULL) )
        return -EINVAL;

    if( !filep->pipe->is_wopen || filep->mode != O_WRITE )
        return -EACCES;

    if( count + filep->pipe->buffer_offset > 4096 )
        return -EOTHERS;

    int buf_ind = filep->pipe->buffer_offset;
    int buf_ind2 = 0;
    while(buf_ind < count){
        filep->pipe->pipe_buff[buf_ind] = buff[buf_ind2];
        buf_ind++;
        buf_ind2++;
    }

    filep->pipe->buffer_offset += count;
    filep->pipe->write_pos = filep->pipe->buffer_offset;

    return count;

    // int ret_fd = -EINVAL; 
    // return ret_fd;
}

int create_pipe(struct exec_context *current, int *fd)
{
    /**
    *  TODO:: Implementation of Pipe Create
    *  Create file struct by invoking the alloc_file() function, 
    *  Create pipe_info struct by invoking the alloc_pipe_info() function
    *  fill the valid file descriptor in *fd param
    *  Incase of Error return valid Error code 
    */
    if( !current )
        return -EINVAL;

    // CHECK VALIDITY OF FD

    int fd_av1 = 3;
    while(current->files[fd_av1]) {
        fd_av1++;
        if( fd_av1 >= MAX_OPEN_FILES )
            return -EOTHERS;
    }
    fd[0] = fd_av1;

    int fd_av2 = fd_av1 + 1;
    while(current->files[fd_av2]) {
        fd_av2++;
        if( fd_av2 >= MAX_OPEN_FILES )
            return -EOTHERS;
    }
    fd[1] = fd_av2;

    struct pipe_info *filep_pipe = alloc_pipe_info();
    if( !filep_pipe )
        return -ENOMEM;
    filep_pipe->read_pos = 0;
    filep_pipe->write_pos = 0;
    filep_pipe->buffer_offset = 0;
    filep_pipe->is_ropen = 1;
    filep_pipe->is_wopen = 1;

    struct file *filep_read = alloc_file();
    if( !filep_read )
        return -ENOMEM;
    filep_read->type = PIPE;
    filep_read->offp = 0;
    filep_read->mode = O_READ;
    filep_read->ref_count = 1;
    filep_read->inode = NULL;
    filep_read->pipe = filep_pipe;
    filep_read->fops->read = pipe_read;
    filep_read->fops->write = pipe_write;
    filep_read->fops->close = generic_close;
    current->files[fd[0]] = filep_read;

    struct file *filep_write = alloc_file();
    if( !filep_write )
        return -ENOMEM;
    filep_write->type = PIPE;
    filep_write->offp = 0;
    filep_write->mode = O_WRITE;
    filep_write->ref_count = 1;
    filep_write->inode = NULL;
    filep_write->pipe = filep_pipe;
    filep_write->fops->read = pipe_read;
    filep_write->fops->write = pipe_write;
    filep_write->fops->close = generic_close;
    current->files[fd[1]] = filep_write;

    return 0;

    // int ret_fd = -EINVAL; 
    // return ret_fd;
}

