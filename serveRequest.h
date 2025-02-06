#pragma once

#define DEFAULT_PAGE_PATH "/test"
#define ERROR_PAGE_PATH "/error.test"
#define ERROR_ERROR_MSG "you even broke the error page, good job\n"

typedef struct gopherConn gopherConn;
void getRequest (int epollfd, gopherConn* conn);
void sendResource (int epollfd, gopherConn* conn);
