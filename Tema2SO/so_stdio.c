#include "so_stdio.h"
#include <sys/types.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

#define BUFFSIZE 4096
#define ERROR 1
#define READ 0
#define WRITE 1
#define NOTWR -1

typedef struct _so_file {
	int fd;
	char *buff;
	int buff_index;
	ssize_t bytes_readed;
	int last_op;
	int error;
	int eof;
	int wb;
	int cursor;
} SO_FILE;

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *file = (SO_FILE *) malloc(sizeof(SO_FILE));

	if (file == NULL)
		return NULL;
	int fd;

	if (strcmp(mode, "r") == 0) {
		fd = open(pathname, O_RDONLY, 0644);
		if (fd < 0) {
			free(file);
			return NULL;
		}
	} else if (strcmp(mode, "r+") == 0) {
		fd = open(pathname, O_RDWR, 0644);
		if (fd < 0) {
			free(file);
			return NULL;
		}
	} else if (strcmp(mode, "w") == 0) {
		fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			free(file);
			return NULL;
		}
	} else if (strcmp(mode, "w+") == 0) {
		fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			free(file);
			return NULL;
		}
	} else if (strcmp(mode, "a") == 0) {
		fd = open(pathname, O_APPEND | O_WRONLY | O_CREAT, 0644);
		if (fd < 0) {
			free(file);
			return NULL;
		}
	} else if (strcmp(mode, "a+") == 0) {
		fd = open(pathname, O_APPEND | O_RDWR | O_CREAT, 0644);
		if (fd < 0) {
			free(file);
			return NULL;
		}
	} else {
		free(file);
		return NULL;
	}
	file->fd = fd;
	file->buff = (char *) malloc(BUFFSIZE * sizeof(char));
	memset(file->buff, 0, BUFFSIZE);
	file->buff_index = -1;
	file->bytes_readed = 0;
	file->last_op = -1;
	file->error = 0;
	file->eof = 0;
	file->wb = 0;
	file->cursor = 0;
	return file;
}

int so_fclose(SO_FILE *stream)
{
	int rc;

	if (stream->last_op == WRITE) {
		rc = so_fflush(stream);
		if (rc < 0) {
			free(stream->buff);
			free(stream);
			return SO_EOF;
		}
	}
	rc = close(stream->fd);
	if (rc < 0) {
		free(stream->buff);
		free(stream);
		return SO_EOF;
	}
	free(stream->buff);
	free(stream);
	return 0;
}

int so_fgetc(SO_FILE *stream)
{
	if (stream == NULL) {
		stream->error = ERROR;
		return SO_EOF;
	}

	if (stream->last_op == WRITE)
		so_fflush(stream);

	if (stream->buff_index == -1) {
		memset(stream->buff, 0, BUFFSIZE);
		stream->bytes_readed = read(stream->fd, stream->buff, BUFFSIZE);
		if (stream->bytes_readed == 0) {
			stream->bytes_readed = 0;
			stream->eof = ERROR;
			return SO_EOF;
		}
		if (stream->bytes_readed < 0) {
			stream->bytes_readed = 0;
			stream->error = ERROR;
			return SO_EOF;
		}
	}
	stream->last_op = READ;
	stream->buff_index++;
	stream->cursor++;
	if (stream->buff_index == stream->bytes_readed-1) {
		stream->buff_index = -1;
		int aux = stream->bytes_readed-1;

		stream->bytes_readed = 0;
		return (int) stream->buff[aux];
	}
	return (int) stream->buff[stream->buff_index];
}

int so_fputc(int c, SO_FILE *stream)
{
	int rc;

	if (stream == NULL) {
		stream->error = ERROR;
		return SO_EOF;
	}

	if (stream->last_op == READ) {
		memset(stream->buff, 0, BUFFSIZE);
		stream->bytes_readed = 0;
		stream->buff_index = -1;
	}

	if (stream->bytes_readed < BUFFSIZE) {
		stream->buff[stream->bytes_readed] = c;
		stream->bytes_readed++;
	} else if (stream->bytes_readed == BUFFSIZE) {
		rc = so_fflush(stream);
		if (rc < 0) {
			stream->error = ERROR;
			return SO_EOF;
		}
		stream->buff[stream->bytes_readed] = c;
		stream->bytes_readed++;
	}
	stream->last_op = WRITE;
	stream->cursor++;
	return c;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytesToRead = 0;
	int c;

	while (bytesToRead < nmemb * size) {
		c = so_fgetc(stream);
		if (c < 0 && (stream->error == ERROR || stream->eof == ERROR))
			return bytesToRead / nmemb;
		*((char *) ptr + bytesToRead) = (char) c;
		bytesToRead++;
	}
	return nmemb;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytesToWrite = 0;
	int c, rc;

	while (bytesToWrite < nmemb * size) {
		c = (int) *((char *) ptr + bytesToWrite);
		rc = so_fputc(c, stream);
		if (rc < 0 && (stream->error == ERROR || stream->eof == ERROR)) {
			stream->error = ERROR;
			return SO_EOF;
		}
		bytesToWrite++;
	}
	return nmemb;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	if (stream->last_op == WRITE)
		so_fflush(stream);

	if (stream->last_op == READ)
		memset(stream->buff, 0, BUFFSIZE);

	int cursor = lseek(stream->fd, offset, whence);

	if (stream->cursor >= 0) {
		stream->cursor += cursor;
		return 0;
	}
	stream->cursor = 0;
	stream->error = ERROR;
	return SO_EOF;
}

long so_ftell(SO_FILE *stream)
{
	if (stream->error == ERROR)
		return SO_EOF;
	return stream->cursor;
}

int so_fflush(SO_FILE *stream)
{
	stream->wb = 0;
	if (stream->bytes_readed == 0)
		return 0;
	stream->wb += write(stream->fd, stream->buff, stream->bytes_readed);
	if (stream->wb >= 0) {
		if (stream->wb < stream->bytes_readed) {
			while (stream->wb < stream->bytes_readed)
				stream->wb += write(stream->fd, stream->buff + stream->wb, stream->bytes_readed - stream->wb);
		}
		memset(stream->buff, 0, BUFFSIZE);
		stream->buff_index = -1;
		stream->bytes_readed = 0;
		return 0;
	}
	stream->error = ERROR;
	return SO_EOF;
}

int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

int so_feof(SO_FILE *stream)
{
	return stream->eof;
}

int so_ferror(SO_FILE *stream)
{
	return stream->error;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	return NULL;
}

int so_pclose(SO_FILE *stream)
{
	return 0;
}
