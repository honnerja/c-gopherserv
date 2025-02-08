#include "string.h"
#include <dirent.h>

#define READ_BUF_SIZE 512

/* responsible for reading files / directories */

enum readerState {
	READER_DIR_HEAD,
	READER_HEAD,
	READER_ENTRY,
	READER_TAIL,
	READER_DIR_TAIL,
	READER_FILE,
	READER_DONE
};

typedef struct reader {
	enum readerState state;
	DIR* dir;
	struct dirent* dirEnt;
	int fd;
	string* buf;
	size_t bufPos;
	char* path;
	/* the following are needed to generate dir entries*/
	char* request
} reader;

void refillBuf(reader* r);
int initReader (reader* r, char* path, char* request);
int isDone(reader* r);
