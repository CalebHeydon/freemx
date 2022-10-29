/*
BSD 2-Clause License

Copyright (c) 2022, Caleb Heydon
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stddef.h>

#define FREEMX_VERSION		"0.1.0"
#define FREEMX_PORT		25
#define FREEMX_BACKLOG 		128
#define FREEMX_BUFFER_SIZE	990 + 1

typedef struct _handle_client_args
{
	int client_fd;
	struct sockaddr_in client_addr;
} handle_client_args;

char fqdn[254];
pthread_mutex_t stderr_mutex;

void print_usage(void)
{
	fprintf(stderr, "usage: freemx <fqdn>\n");
}

void *handle_client(void *args)
{
	handle_client_args *real_args = args;

	int client_fd = real_args->client_fd;
	struct sockaddr_in client_addr = real_args->client_addr;
	free(args);

	char *address = inet_ntoa(client_addr.sin_addr);
	int port = ntohs(client_addr.sin_port);

	pthread_mutex_lock(&stderr_mutex);
	fprintf(stderr, "%s:%d\n", address, port);
	pthread_mutex_unlock(&stderr_mutex);

	FILE *client_write = fdopen(client_fd, "wb");
	FILE *client_read = fdopen(client_fd, "rb");

	char *buffer = malloc(FREEMX_BUFFER_SIZE);
	if (buffer == NULL)
	{
		pthread_mutex_lock(&stderr_mutex);
		fprintf(stderr, "Out of memory\n");
		pthread_mutex_unlock(&stderr_mutex);
		goto FREEMX_HANDLE_CLIENT_DONE;
	}
	memset(buffer, 0, FREEMX_BUFFER_SIZE);

	snprintf(buffer, FREEMX_BUFFER_SIZE, "220 %s SMTP freemx\r\n", fqdn);
	fwrite(buffer, 1, strlen(buffer), client_write);
	fflush(client_write);

	int state = 0;
	while (true)
	{
		if (fgets(buffer, FREEMX_BUFFER_SIZE - 1, client_read) == NULL)
		{
			free(buffer);
			goto FREEMX_HANDLE_CLIENT_DONE;
		}
		fprintf(stderr, "%s", buffer);

		char *substr = strstr(buffer, "QUIT");
		if (substr == buffer)
		{
			const char *bye = "221\r\n";
			fwrite(bye, 1, strlen(bye), client_write);
			fflush(client_write);
			break;
		}

		bool exit = false;
		bool invalidCommand = false;

		switch (state)
		{
		case 0:
		{
			substr = strstr(buffer, "HELO ");
			if (substr == buffer)
			{
				state = 1;
				const char *ok = "250\r\n";
				fwrite(ok, 1, strlen(ok), client_write);
				fflush(client_write);
			}
			else
				invalidCommand = true;
			break;
		}
		default:
			exit = true;
			break;
		}

		if (exit)
			break;

		if (invalidCommand)
		{
			const char *error = "502\r\n";
			fwrite(error, 1, strlen(error), client_write);
			fflush(client_write);
		}
	}

	free(buffer);

FREEMX_HANDLE_CLIENT_DONE:
	fclose(client_write);
	fclose(client_read);


	return NULL;
}

int main(int argc, char **argv)
{
	fprintf(stderr, "freemx version %s\nCopyright (c) 2022, Caleb Heydon\n", FREEMX_VERSION);

	if (argc < 2)
	{
		print_usage();
		return -1;
	}

	size_t fqdn_len = strlen(argv[1]);
	memcpy(fqdn, argv[1], fqdn_len < sizeof(fqdn) ? fqdn_len : sizeof(fqdn));

	if (pthread_mutex_init(&stderr_mutex, NULL) != 0)
	{
		fprintf(stderr, "Unable to initialize mutex\n");
		return -1;
	}

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		fprintf(stderr, "Unable to create socket\n");
		return -1;
	}

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(FREEMX_PORT);
	server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");

	if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
	{
		fprintf(stderr, "Unable to bind to port\n");
		close(server_fd);
		return -1;
	}

	if (listen(server_fd, FREEMX_BACKLOG) == -1)
	{
		fprintf(stderr, "Unable to listen\n");
		close(server_fd);
		return -1;
	}

	while (true)
	{
		struct sockaddr_in client_addr;
		socklen_t addr_size = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &addr_size);
		if (client_fd == -1)
			continue;

		handle_client_args *thread_args = malloc(sizeof(handle_client_args));
		if (thread_args == NULL)
		{
			pthread_mutex_lock(&stderr_mutex);
			fprintf(stderr, "Out of memory\n");
			pthread_mutex_unlock(&stderr_mutex);

			close(client_fd);
			continue;
		}
		memset(thread_args, 0, sizeof(handle_client_args));

		thread_args->client_fd = client_fd;
		thread_args->client_addr = client_addr;

		pthread_t thread;
		pthread_create(&thread, NULL, handle_client, (void *) thread_args);
	}

	return 0;
}
