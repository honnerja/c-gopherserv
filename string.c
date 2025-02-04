#include <string.h>
#include <stdlib.h>
string* strAlloc(size_t len) {
	string* s = malloc(sizeof(string) + len);
	if (len > 1000) puts("big str alloc");
	s->size = len;
	s->len = 0;
	return s;
}

void strFree(string* str) {
	free(str);
}

/* returns the amount of chars that could be appended */
size_t strAppend(string* str, char* content, size_t len) {
	if (len + str->len > str->size) {
		len = str->size - str->len;
	}
	memcpy(str->content + str->len, content, len);
	str->len += len;
	return len;
}

/* truncates string
 * returns 0 on success, -1 on fail
 */
int strTruncate(string* str, size_t len) {
	if (len > str->size) {
		return -1;
	}
	if (len < str->len) {
		str->len = len;
	}
	return 0;
}
