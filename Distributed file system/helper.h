#include <iostream>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <boost/program_options.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <chrono>
#include <map>
#include <algorithm>
#include <filesystem>
#include <dirent.h>
#include <fcntl.h>
#include <csignal>
#include "err.h"

using namespace std;
using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>; 

enum Listen_mode {none, discover, search_list};
enum Server_mode {get_file, upload_file};
enum Message_type {smpl, cmplx, bad};

typedef struct message {
  Message_type type;
  ssize_t size;
  string cmd;
  uint64_t cmd_seq;
  uint64_t param;
  string data;
}message;

typedef struct server_timer {
  bool is_set;
  time_point beg;
  time_point end;
}server_timer;

message init_smpl_message (string cmd, uint64_t *cmd_seq, string data);

message init_cmplx_message (string cmd, uint64_t *cmd_seq, uint64_t param, string data);

message get_message (const char* buff, int size, sockaddr_in address);

void convert_message (char* buff, message m);

void start_timer (time_point &beg, vector <struct pollfd> &polls);

bool process_message (message m, map<string, vector<int>> &active_seq, struct sockaddr_in address);

void add_files (string data, map<string, string> &file_list, string sin_addr);

ssize_t check_file_size (string path);

int TCP_connect (int port, struct sockaddr_in server_adress, string file_name, string what);

void remove_descriptor (vector <struct pollfd> &polls, vector <FILE *> &opened_files, size_t *pos);

void print_log (map<int, pair<string, sockaddr_in>> &log_helper, int fd, string what);

void print_error_log (map<int, pair<string, sockaddr_in>> &log_helper, int fd, string what, string reason);

void handle_upload_error (map<int, pair<string, sockaddr_in>> &log_helper, string reason,
    vector<struct pollfd> &polls, vector <FILE *> &opened_files, size_t *pos,
    map <string, vector<pair <string, int>>> &upload_candidates);

void handle_download_error (map<int, pair<string, sockaddr_in>> &log_helper, string reason,
      vector<struct pollfd> &polls, vector <FILE *> &opened_files, size_t *pos); 

void find_files (string folder, set<string> &files, size_t &space);

void start_server_timer (server_timer &timer);

bool check_timeout (server_timer &timer, int timeout);

double get_min_time (vector<struct pollfd> &polls, int timeout, map<int, server_timer> &tcp_timres);

void remove_sock (vector<struct pollfd> &polls, size_t *pos);

string convert_file_list (set<string> &files, set<string>::iterator &it, size_t MTU);

void filter_files (set<string> &good_files, set<string> &files, string pattern);

bool check_file (set<string> &files, set<string> &uploading_files, string file_name, uint64_t &space, uint64_t size);

int tcp_listen (struct sockaddr_in &server_address);

void print_package_error (string s, sockaddr_in address);

void print_error_no_address (string file_name, string what, string reason);
