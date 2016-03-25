/*
 * Description: A variable length circular queue, support single process or
 *              thread write and single process or thread read.
 *              Support use file as storage.
 *     History: yang@haipo.me, 2013/06/08, create
 */

# ifndef _QUEUE_H_
# define _QUEUE_H_

# include <stdint.h>
# include <stddef.h>

typedef struct queue_t{
    void   *memory;
    void   *read_buf;
    size_t read_buf_size;
} queue_t;

int queue_init(queue_t *queue, key_t shm_key, uint32_t mem_size, char *file, size_t file_max_size);

int queue_push(queue_t *queue, void *data, uint32_t size);
int queue_pop(queue_t *queue, void **data, uint32_t *size);

void queue_fini(queue_t *queue);

typedef struct queue_info {
    uint32_t mem_num;
    uint32_t mem_size;
    size_t file_num;
    size_t file_size;
} queue_info;

void queue_state(queue_t *queue, queue_info *info);

# endif

