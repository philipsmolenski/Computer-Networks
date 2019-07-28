#ifndef _HELPER_
#define _HELPER_

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t size;
    char* content;
} Str;

Str init (char * buf);

Str append (Str s, char* buf);

// zwracana jest liczba plików
int printFiles (char* buf, uint32_t len);

char* findFile (uint32_t num, char *buf, uint32_t len, uint16_t* nameLen);

Str getFiles ();

bool checkExistance (char* dirName, char* fileName);

void closeSock (int sockNum);

bool serverWrite (int sockNum, void* buf, size_t count, bool *connect);

bool serverRead (int sockNum, char* buf, size_t size, size_t nmemb, FILE *f, bool* connect);

bool readAll (int sockNum, void* buf, size_t count);

#endif
