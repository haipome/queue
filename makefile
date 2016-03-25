all:
	gcc test_server.c queue.c -g -Wall -o test_server
	gcc test_client.c queue.c -g -Wall -o test_client

clean:
	rm -rf test_server test_client
