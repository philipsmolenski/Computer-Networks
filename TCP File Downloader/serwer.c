#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "err.h"
#include "helper.h"

#define BUFFSIZE 524288
#define MAX_FILENAME_LEN 70000

int main (int argc, char** argv) {
    if (argc < 2 || argc > 4)
        fatal("wrong args number");

    int sock, msgSock = 0, pathLen;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;
    uint16_t read2, id, nameLen;
    uint32_t read4, beg, len, filesLen, fileEnd, id2, writeLen;
    char fileName[MAX_FILENAME_LEN];
    char buf[BUFFSIZE];
    bool connect = true;
    bool fail1 , fail2, fail3;
    int portNum = 6543;

    if (argc == 3) 
        portNum = atoi(argv[2]);

    sock = socket (PF_INET, SOCK_STREAM, 0); 
    if (sock <0)
        syserr ("socket");

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(portNum);

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
    syserr ("bind");

    // switch to listening (passive open)
    if (listen(sock,  SOMAXCONN) < 0)
    syserr("listen");

    printf("Oczekuję klienta na porcie: %hu\n", ntohs(server_address.sin_port));

    while (true) {
        if (connect) {
            connect = false;
            client_address_len = sizeof(client_address);
            msgSock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
            printf("Rozpoczynam połączenie z klientem\n");
            
            if (msgSock < 0)
                syserr("accept");
        }

        if (!readAll(msgSock, &read2, 2)) {
            connect = true;
            continue;
        }

        id = ntohs(read2);
        if (id == 1) {
            printf("Otrzymano prośbę o przesłanie plików\n");
            Str files = getFiles(argv[1]);

            if (files.size == -1) {
                printf("No files in directory\n");
                connect = true;
                closeSock(msgSock);
                continue;
            }

            printf("Dostępne pliki: %.*s\n", files.size, files.content);

            id = htons(1);
            if (!serverWrite(msgSock, &id, 2, &connect)) {
                free (files.content);
                continue;
            }

            filesLen = htonl(files.size);
            if (!serverWrite(msgSock, &filesLen, 4, &connect)) {
                free (files.content);
                continue;
            }

            if (!serverWrite(msgSock, files.content, files.size, &connect)) {
                free (files.content);
                continue;
            }

            free(files.content);
        }

        else if (id == 2) {
            if (!readAll (msgSock, &read4, 4)) {
                connect = true;
                continue;
            }
            
            beg = ntohl(read4);
            
            if (!readAll(msgSock, &read4, 4)) {
                connect = true;
                continue;
            }

            len = ntohl(read4);
            
            if (!readAll (msgSock, &read2, 2)) {
                connect = true;
                continue;
            }

            nameLen = ntohs(read2);
            
            if (!readAll(msgSock, fileName, nameLen)) {
                connect = true;
                continue;
            }

            printf("Otrzymano prośbę o wysłanie fragmentu pliku, parametry:\n");
            printf("początek - %" PRIu32 "; długość - %" PRIu32 "; długość nazwy - %" PRIu32 "; nazwa - %.*s\n", beg, len, nameLen, nameLen, fileName);

            pathLen = nameLen + strlen(argv[1]) + 2;
            
            char path[pathLen];
            strcpy(path, argv[1]);
            path[strlen(argv[1])] = '/';
            strncpy(path + strlen(argv[1]) + 1, fileName, nameLen);
            path[pathLen - 1] = 0;
            fileName[nameLen] = 0;
            fail1 = false;
            fail2 = false;
            fail3 = false;

            if (beg + len <= beg)
                fail3 = true;

            if (!checkExistance(argv[1], fileName))
                fail1 = true;

            FILE *f;
            f = fopen(path, "rb");
            if (f == NULL)
                fail1 = true;
            
            else {
                fseek(f, 0, SEEK_END);
                fileEnd = ftell(f);
                if (beg >= fileEnd)
                    fail2 = true;

                if (beg + len > fileEnd)
                    len = fileEnd - beg;                
            }

            if (fail1 || fail2 || fail3) {
                id = htons(2);
                if (!serverWrite(msgSock, &id, 2, &connect)) 
                    continue;

                if (fail1) 
                    id2 = htonl(1);
            
                if (fail2)
                    id2 = htonl(2);

                if (fail3) 
                    id2 = htonl(3);

                if (f != NULL)
                    fclose(f);

                if (!serverWrite(msgSock, &id2, 4, &connect))
                    continue;

            }

            else {
                id = htons(3);
                if (!serverWrite(msgSock, &id, 2, &connect)) 
                    continue;

                writeLen = htonl(len);
                if (!serverWrite(msgSock, &writeLen, 4, &connect)) {
                    continue;
                }

                while (len > BUFFSIZE) {
                    fseek(f, beg, SEEK_SET);
                    beg += BUFFSIZE;
                    len -= BUFFSIZE;
                    if (!serverRead(msgSock, buf, 1, BUFFSIZE, f, &connect)) 
                        len = 0;

                    if (!serverWrite(msgSock, buf, BUFFSIZE, &connect)) 
                        len = 0; 
                }

                if (len == 0)
                    continue;

                fseek(f, beg, SEEK_SET);

                if(!serverRead(msgSock, buf, 1, len, f, &connect))
                    continue;

                if (!serverWrite(msgSock, buf, len, &connect))
                    continue;

                fclose(f);
            }

        }

        else {
            printf("wrong answer from client: expected values: 1 or 2\n");
            connect = true;
            closeSock(msgSock);
        }

    }

    return 0;
}