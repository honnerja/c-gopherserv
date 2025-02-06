#pragma once
#include <stddef.h>
typedef struct string {
	size_t size;
	size_t len;
	char content[];
} string;

string* strAlloc(size_t len);
void strFree(string* str);

/* returns the amount of chars that could be appended */
size_t strAppend(string* str, char* content, size_t len); 

/* truncates string
 * returns 0 on success, -1 on fail
 */
int strTruncate(string* str, size_t len); 


