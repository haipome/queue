# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <error.h>
# include <errno.h>
# include <unistd.h>

# include "queue.h"

int main()
{
    queue_t queue;
    if (queue_init(&queue, 10000, 1024 * 1024, "test.queue", 0) < 0)
        error(EXIT_FAILURE, errno, "queue_init fail");

    char path[100];
    sprintf(path, "%d.txt", getpid());

    FILE *fp = fopen(path, "w+");
    if (fp == NULL)
        error(EXIT_FAILURE, errno, "open %s fail", path);

    while (1)
    {
        void *p;
        uint32_t len;

        int ret = queue_pop(&queue, &p, &len);

        if (ret >= 0)
        {
            fprintf(fp, "%s\n", (char *)p);
            fflush(fp);
        }
        /*
        else
        {
            printf("fail, %m\n");

            break;
        }
        */
    }

    fclose(fp);

    queue_fini(&queue);

    return 0;
}

