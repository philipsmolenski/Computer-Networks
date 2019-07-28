#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>
#include <inttypes.h>

#include "err.h"
#include "helper.h"

#define DEFAULT_PORT "6543"
#define BUFFSIZE 524288
#define MAX_FILENAME_LEN 255

struct __attribute__((__packed__)) FileRequest {
  uint16_t requestId;
  uint32_t beg;
  uint32_t len;
  uint16_t nameLen;
  char name[MAX_FILENAME_LEN];
};

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4)
        fatal("wrong args number");

    int sock, err;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;
    Str service;
    uint16_t id, nameLen, read2;
    uint32_t len, beg, end, id2, num, read4;
    ssize_t dataSize;
    char buf[BUFFSIZE];
    struct FileRequest fileRequest;

    if (argc < 3) 
        service = init(DEFAULT_PORT);
    
    else
        service = init(argv[2]);

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(argv[1], service.content, &addr_hints, &addr_result);
    free(service.content);
    
    if (err == EAI_SYSTEM)
        syserr("getaddrinfo: %s", gai_strerror(err));
    
    else if (err != 0) 
        fatal("getaddrinfo: %s", gai_strerror(err));
    
    // initialize socket according to getaddrinfo result
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    
    if (sock < 0)
        syserr("socket");
                    
    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0)
        syserr("connect");

    freeaddrinfo(addr_result);

    id = htons(1);
    if (write(sock, &id , 2) != 2)
        syserr("partial/failed write");

    if (!readAll(sock, &id, 2))
        syserr("server disconnected");

    if (ntohs(id) != 1)
        fatal("wrong answer from server: expected value: 1");

    if (!readAll(sock, &read4, 4))
        syserr("server disconnected");

    len = ntohl(read4);    
    char files[len];
    
    if (!readAll(sock, files, len))
        syserr("server disconnected");
    int count = printFiles(files, len);

    printf("Podaj numer wybranego pliku\n");
    
    while (true) {
        err = scanf("%" SCNd32 , &num);
        if (err != 1)
            fatal("error in read from user");

        if (num > count) 
            printf("Wybrano zbyt dużą liczbę, spróbuj jeszcze raz\n");

        else if (num == 0)
            printf("Proszę wybrać dodatnią liczbę\n");

        else break;
    }

    printf("Podaj adres początkowy fragmentu\n");
    
    err = scanf("%" SCNd32, &beg);
    if (err != 1)
        fatal("error in read from user");
    
    printf("Podaj adres końcowy fragmentu\n");

    while (true) {
        err = scanf("%" SCNd32, &end);
        if (err != 1)
            fatal("error in read from user");

        if (end < beg) 
            printf("Adres końcowy nie może być mniejszy, niż początkowy, spróbuj jeszcze raz\n");

        else break;
    }
    
    char* fileName = findFile(num, files, len, &nameLen);
    fileRequest.requestId = htons(2);
    fileRequest.beg = htonl(beg);
    fileRequest.len = htonl(end - beg);
    strncpy(fileRequest.name, fileName, nameLen);
    fileRequest.nameLen = htons(nameLen);
    free(fileName);
    
    dataSize = 12 + nameLen;
    if (write(sock, &fileRequest, dataSize) != dataSize)
      syserr("partial / failed write");
    
    if (!readAll(sock, &read2, 2))
        syserr("server disconnected");

    id = ntohs(read2);

    if (id == 2) {
        if (!readAll(sock, &read4, 4))
            syserr("server disconnected");
        
        id2 = ntohl(read4);

        if (id2 == 1) 
            printf("Zła nazwa pliku\n");

        if (id2 == 2)
            printf("Nieprawidłowy adres początku fragmentu\n");

        if (id2 == 3)
            printf("Podano nieprawidłowy rozmiar fragmentu\n");
    }

    else if (id != 3)
        fatal("wrong answer from server, expedted values: 2 or 3");

    else {
        if (!readAll(sock, &read4, 4))
            syserr("server disconnected");

        len = ntohl(read4);

        struct stat st;
        if (stat("tmp", &st) == -1) 
            if(mkdir("tmp", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1)
                fatal("unable to create directory");
        
        char path [nameLen + 5];
        strcpy (path, "tmp/");
        strncpy (path + 4, fileRequest.name, nameLen);
        path[nameLen + 4] = 0;
        FILE *f;

        f = fopen(path, "rb+");
        if (f == NULL) {
            f = fopen(path, "wb");
            if (f == NULL)
                syserr("unable to open file");
        }

        while (len > BUFFSIZE) {
            if (!readAll(sock, buf, BUFFSIZE))
                syserr("server disconnected");

            fseek (f , beg , SEEK_SET);
            beg += BUFFSIZE;
            len -= BUFFSIZE;
            if (fwrite(buf, 1, BUFFSIZE, f) != BUFFSIZE)
                syserr("partial / failed write to file");
        }

        if (!readAll(sock, buf, len))
            syserr("server disconnected");
  
        fseek (f ,beg ,SEEK_SET);
        
        if (fwrite(buf, 1, len, f) != len)
            syserr("partial / failed write to file");

        fclose(f);
    
    }
    
    printf("Koniec połączenia\n");
    if (close(sock) < 0)
        syserr("close");

  return 0;
}