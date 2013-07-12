# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <error.h>
# include <errno.h>
# include <inttypes.h>

# include "queue.h"

int main()
{
    queue_t queue;
    if (queue_init(&queue, "test", 10000, 1024 * 1024, "test.queue", 0) < 0)
        error(EXIT_FAILURE, errno, "queue_init fail");

    int i;
    for (i = 0; i < 100000; ++i)
    {
        char buf[100] = { 0 };
        snprintf(buf, sizeof(buf), "%d", i);
        uint32_t len = strlen(buf) + 1;

        if (queue_push(&queue, buf, len) < 0)
            error(EXIT_FAILURE, errno, "queue_push fail");
    }

    printf("%"PRIu64"\n", queue_len(&queue));
    printf("%"PRIu64"\n", queue_num(&queue));

    queue_fini(&queue);

    return 0;
}

