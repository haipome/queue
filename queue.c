/*
 * Description: A variable length circular queue, support single process or
 *              thread write and single process or thread read.
 *              Support use file as storage.
 *     History: yang@haipo.me, 2013/06/08, create
 */


# undef  _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 64

# define MAGIC_NUM 20130610

# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <stdint.h>
# include <stdbool.h>
# include <assert.h>
# include <limits.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/ipc.h>
# include <sys/shm.h>

# include "queue.h"

# pragma pack(1)
struct queue_head {
    uint32_t magic;
    uint64_t shm_key;
    uint32_t mem_size;
    uint32_t mem_use;
    uint32_t mem_num;
    uint32_t pos_head;
    uint32_t pos_tail;

    char     file[512];
    uint64_t file_max_size;
    uint64_t file_start;
    uint64_t file_end;
    uint64_t file_num;
};
# pragma pack()

static void *get_shm(key_t key, size_t size, int flag)
{
    int shm_id = shmget(key, size, flag);
    if (shm_id < 0)
        return NULL;
    void *p = shmat(shm_id, NULL, 0);
    if (p == (void *)-1)
        return NULL;
    return p;
}

static int get_or_create_shm(key_t key, size_t size, void **addr)
{
    if ((*addr = get_shm(key, size, 0666)) != NULL)
        return 0;
    if ((*addr = get_shm(key, size, 0666 | IPC_CREAT)) != NULL)
        return 1;
    return -1;
}

int queue_init(queue_t *queue, key_t shm_key, uint32_t mem_size, char *file, uint64_t file_max_size)
{
    size_t real_mem_size = sizeof(struct queue_head) + mem_size;
    void *memory = NULL;
    bool shm_exist = false;
    int ret = get_or_create_shm(shm_key, real_mem_size, &memory);
    if (ret < 0) {
        return -1;
    } else if (ret == 0) {
        shm_exist = true;
    }

    volatile struct queue_head *head = memory;
    if (shm_exist == false) {
        memset(memory, 0, sizeof(struct queue_head));
        head->magic = MAGIC_NUM;
        head->shm_key  = shm_key;
        head->mem_size = mem_size;
        if (file) {
            if (strlen(file) >= sizeof(head->file))
                return -1;
            strcpy((char *)head->file, file);
            remove((char *)head->file);
            head->file_max_size = file_max_size;
        }
    } else if (head->magic != MAGIC_NUM) {
        return -1;
    }

    memset(queue, 0, sizeof(*queue));
    queue->memory = memory;

    return 0;
}

static int write_file(queue_t *queue, void *data, uint32_t size)
{
    volatile struct queue_head *head = queue->memory;
    if (head->file_max_size) {
        if ((head->file_end + (sizeof(size) + size)) > head->file_max_size)
            return -1;
    }

    FILE *fp = fopen((char *)head->file, "a+");
    if (fp == NULL)
        return -1;
    if (fseeko(fp, head->file_end, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (fwrite(&size, sizeof(size), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    if (fwrite(data, size, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    head->file_end += (sizeof(size) + size);
    __sync_fetch_and_add(&head->file_num, 1);

    return 0;
}

static void putmem(queue_t *queue, uint32_t *pos_tail, void *data, uint32_t size)
{
    volatile struct queue_head *head = queue->memory;
    void *buf = queue->memory + sizeof(struct queue_head);
    uint32_t tail_left = head->mem_size - *pos_tail;

    if (tail_left < size) {
        memcpy(buf + *pos_tail, data, tail_left);
        *pos_tail = size - tail_left;
        memcpy(buf, data + tail_left, *pos_tail);
    } else {
        memcpy(buf + *pos_tail, data, size);
        *pos_tail += size;
    }
}

int queue_push(queue_t *queue, void *data, uint32_t size)
{
    volatile struct queue_head *head = queue->memory;
    assert(head->magic == MAGIC_NUM);

    if ((head->mem_size - head->mem_use) < (sizeof(size) + size)) {
        if (head->file[0]) {
            return write_file(queue, data, size);
        }
        return -1;
    }
    if (head->file[0] && head->file_end && head->file_num == 0) {
        remove((char *)head->file);
        head->file_start = 0;
        head->file_end   = 0;
    }

    uint32_t pos_tail = head->pos_tail;
    putmem(queue, &pos_tail, &size, sizeof(size));
    putmem(queue, &pos_tail, data, size);
    head->pos_tail = pos_tail;

    __sync_fetch_and_add(&head->mem_use, sizeof(size) + size);
    __sync_fetch_and_add(&head->mem_num, 1);

    return 0;
}

static void *alloc_read_buf(queue_t *queue, uint32_t size)
{
    if (queue->read_buf == NULL || queue->read_buf_size < size) {
        void  *buf = queue->read_buf;
        size_t buf_size = queue->read_buf_size;

        if (buf == NULL)
            buf_size = 8;
        while (buf_size < size)
            buf_size *= 2;
        buf = realloc(buf, buf_size);
        if (buf == NULL)
            return NULL;

        queue->read_buf = buf;
        queue->read_buf_size = buf_size;
    }

    return queue->read_buf;
}

static int read_file(queue_t *queue, void **data, uint32_t *size)
{
    volatile struct queue_head *head = queue->memory;

    errno = 0;
    FILE *fp = fopen((char *)head->file, "r");
    if (fp == NULL) {
        if (errno == ENOENT) { /* No such file or directory */
            head->file_num   = 0;
            head->file_start = 0;
            head->file_end   = 0;
        }
        return -1;
    }
    if (fseeko(fp, head->file_start, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    uint32_t msg_size = 0;
    if (fread(&msg_size, sizeof(msg_size), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    *data = alloc_read_buf(queue, msg_size);
    if (*data == NULL) {
        fclose(fp);
        return -1;
    }
    if (fread(*data, msg_size, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *size = msg_size;

    head->file_start += sizeof(msg_size) + msg_size;
    __sync_fetch_and_sub(&head->file_num, 1);

    return 1;
}

static void getmem(queue_t *queue, uint32_t *pos_head, void *data, uint32_t size)
{
    volatile struct queue_head *head = queue->memory;
    void *buf = queue->memory + sizeof(struct queue_head);
    uint32_t tail_left = head->mem_size - *pos_head;

    if (tail_left < size) {
        memcpy(data, buf + *pos_head, tail_left);
        *pos_head = size - tail_left;
        memcpy(data + tail_left, buf, *pos_head);
    } else {
        memcpy(data, buf + *pos_head, size);
        *pos_head += size;
    }
}

static int check_mem(queue_t *queue, size_t size)
{
    volatile struct queue_head *head = queue->memory;
    if (head->mem_use < size) {
        head->mem_use = 0;
        head->mem_num = 0;
        head->pos_head = head->pos_tail = 0;
        return -1;
    }

    return 0;
}

int queue_pop(queue_t *queue, void **data, uint32_t *size)
{
    volatile struct queue_head *head = queue->memory;
    assert(head->magic == MAGIC_NUM);

    if (head->mem_num == 0) {
        if (head->file[0] && head->file_num) {
            return read_file(queue, data, size);
        }
        return 0;
    }

    uint32_t msg_size = 0;
    uint32_t pos_head = head->pos_head;
    if (check_mem(queue, sizeof(msg_size)) < 0)
        return -1;
    getmem(queue, &pos_head, &msg_size, sizeof(msg_size));

    *data = alloc_read_buf(queue, msg_size);
    if (*data == NULL)
        return -1;
    *size = msg_size;

    if (check_mem(queue, (sizeof(msg_size) + msg_size)) < 0)
        return -1;
    getmem(queue, &pos_head, *data, msg_size);
    head->pos_head = pos_head;

    __sync_fetch_and_sub(&head->mem_use, sizeof(msg_size) + msg_size);
    __sync_fetch_and_sub(&head->mem_num, 1);

    return 1;
}

void queue_fini(queue_t *queue)
{
    volatile struct queue_head *head = queue->memory;
    assert(head->magic == MAGIC_NUM);

    if (head->shm_key)
        shmdt(queue->memory);
    if (queue->read_buf)
        free(queue->read_buf);
}

void queue_state(queue_t *queue, queue_info *info)
{
    volatile struct queue_head *head = queue->memory;
    assert(head->magic == MAGIC_NUM);

    info->mem_num   = head->mem_num;
    info->mem_size  = head->mem_use;
    info->file_num  = head->file_num;
    info->file_size = head->file_end - head->file_start;
}

