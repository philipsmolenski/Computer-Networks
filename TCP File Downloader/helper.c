#include <stdlib.h> 
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#include "err.h"
#include "helper.h"

Str init (char * buf) {
    Str s;
    s.size = strlen (buf);
    s.content = (char*)malloc(s.size + 1);

    if (s.content == NULL)
        fatal("bad malloc");

    strcpy (s.content, buf);

    return s;
}

// Str append (Str s, char* buf) __attribute__((__warn__unused__result__));
Str append (Str s, char* buf) {
    uint32_t len = strlen(buf);
    uint32_t oldSize = s.size;
    s.size += len;

    char* temp = (char*)realloc(s.content, s.size + 1);

    if (temp == NULL)
        fatal ("bad realloc");

    s.content = temp;
    strcpy (s.content + oldSize, buf);

    return s;
}

int printFiles (char* buf, uint32_t len) {
    printf ("Dostępne pliki:\n");

    uint32_t beg = 0;
    int counter = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] == '|') {
            printf("%d.%.*s\n",counter, i - beg, buf + beg);
            counter++;
            beg = i + 1;
        }
    }

    printf ("%d.%.*s\n",counter, (int)len - beg, buf + beg);    

    return counter;
}

char* findFile (uint32_t num, char* buf, uint32_t len, uint16_t* nameLen) {
    char* ret;
    int counter = 1;
    uint32_t beg = 0;
    uint32_t end = 0;
    uint32_t i = 0;

    while (counter < num && i < len) {
        i++;
        if (buf[i] == '|') {
            counter++;
            beg = i + 1;
        }
    }

    if (i == len)
        return NULL;

    for (i = beg; i < len; i++) {
        if (buf[i] == '|') {
            end = i;
            break;
        }
    }
    
    if (end == 0)
        end = len;

    ret = (char*)malloc(end - beg);
    
    if (ret == NULL)
        fatal("bad alloc");

    strncpy(ret, buf + beg, end - beg);
    *nameLen = end - beg;

    
    return ret;
}

Str getFiles (char* dirName) {
    char line[2] = "|";
    Str ret = init("");
    Str root = init(dirName);
    root = append(root, "/");
    DIR *d;
    struct dirent *dir;
    d = opendir (dirName);

    if (d == NULL) {
        fatal("error in opening directory");
    }

    while ((dir = readdir(d)) != NULL) {
        struct stat s = {0};
        Str path = init(root.content);
        path = append(path, dir->d_name);
        if (stat(path.content, &s) == 0) {
            if (S_ISREG(s.st_mode)) {
                ret = append(ret, dir->d_name);
                ret = append(ret, line);
            }
        }
        free(path.content);
    }

    free(root.content);
    closedir(d);
    ret.size--;

    return ret;
}

bool checkExistance (char* dirName, char* fileName) {
    bool ret = false;
    DIR *d;
    Str root = init(dirName);
    root = append(root, "/");
    struct dirent *dir;
    d = opendir(dirName);

    if (d == NULL) {
        fatal("error in opening directory");
    }

    while ((dir = readdir(d)) != NULL) {
        struct stat s = {0};
        Str path = init(root.content);
        path = append(path, dir->d_name);
        if (stat(path.content, &s) == 0) 
            if (S_ISREG(s.st_mode) && strcmp(fileName, dir->d_name) == 0)
                ret = true;

        free(path.content);
    }

    closedir(d);
    free(root.content);
    return ret;
}

void closeSock (int sockNum) {
    printf ("Koniec połączenia\n");
    if (close(sockNum) < 0)
        syserr ("close");
}

bool serverRead (int sockNum, char* buf, size_t size, size_t nmemb, FILE *f, bool* connect) {
    if (fread(buf, size, nmemb, f) != nmemb) {
        *connect = true;
        printf ("failed / partial read from file\n");
        closeSock (sockNum);
        return false;
    }

    return true;
}

bool serverWrite (int sockNum, void* buf, size_t count, bool *connect) {
    if (write(sockNum, buf, count) != count) {
        *connect = true;
        printf ("failed / partial write\n");
        closeSock (sockNum);
        return false;
    }

    return true;
}

// zwraca false, gdy przeciwna strona wyśle informację o końcu pracy
bool readAll (int sockNum, void* buf, size_t count) {
    size_t prev_len = 0;
    ssize_t len;

    while (prev_len < count) {
        len = read (sockNum, buf + prev_len, count - prev_len);

        if (len <= 0) {
            closeSock (sockNum);
            return false; //druga strona się rozłączyła
        }

        prev_len += len;
    }

    return true;
}
