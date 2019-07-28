#include "helper.h"

using namespace std;
using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>; 

message init_smpl_message (string cmd, uint64_t *cmd_seq, string data) {
  (*cmd_seq)++;
  message m;
  m.type = smpl;
  m.cmd = cmd;
  m.cmd_seq = *cmd_seq;
  m.data = data;
  m.size = data.size() + 18;
  return m;
}

message init_cmplx_message (string cmd, uint64_t *cmd_seq, uint64_t param, string data) {
  (*cmd_seq)++;
  message m;
  m.type = cmplx;
  m.cmd = cmd;
  m.cmd_seq = *cmd_seq;
  m.param = param;
  m.data = data;
  m.size = data.size() + 26;
  return m;
}

message get_message (const char* buff, int size, sockaddr_in address) {
  message m;
  m.size = size;
  char cmd[11];
  uint64_t cmd_seq, param;

  if (m.size < 18) {
    m.type = bad;
    print_package_error("Message is too short.", address);
    return m;
  }

  memcpy(cmd, buff, 10);
  cmd[10] = '\0';
  m.cmd = cmd;

  if (m.cmd == "HELLO" || m.cmd == "LIST" || m.cmd == "DEL"
    || m.cmd == "NO_WAY" || m.cmd == "MY_LIST" || m.cmd == "GET")
    m.type = smpl;

  else if (m.cmd == "GOOD_DAY" || m.cmd == "CONNECT_ME"
    || m.cmd == "ADD" || m.cmd == "CAN_ADD")
    m.type = cmplx;

  else
    m.type = bad;

  if (m.type == bad) {
    print_package_error("Unknown message type.", address);
    return m;
  }

  if (m.size < 26 && m.type == cmplx) {
    m.type = bad;
    print_package_error("Message is too short.", address);
    return m;
  }


  memcpy(&cmd_seq, buff + 10, 8);
  m.cmd_seq = be64toh(cmd_seq);

  if (m.type == smpl) {
    char data[size - 17];
    data[size - 18] = '\0';
    memcpy(&data, buff + 18, size - 18);
    m.data = data;
  }

  else if (m.type == cmplx) {
    memcpy(&param, buff + 18, 8);
    m.param = be64toh(param);
    char data[size - 25];
    data[size - 26] = '\0';
    memcpy(&data, buff + 26, size - 26);
    m.data = data;
  }

  return m;
}

void convert_message (char* buff, message m) {
  const char* cmd = m.cmd.c_str();
  const char* data = m.data.c_str();
  uint64_t cmd_seq = htobe64(m.cmd_seq);
  uint64_t param = htobe64(m.param);
  memcpy(buff, cmd, m.cmd.size());
  memset(buff + m.cmd.size(), 0, 10 - m.cmd.size());
  memcpy(buff + 10, &cmd_seq, 8);
  
  if (m.type == smpl)
    memcpy(buff + 18, data, m.size - 18);
  
  else {
    memcpy(buff + 18, &param, 8);
    memcpy(buff + 26, data, m.size - 26);
  }
}

void start_timer (time_point &beg, vector <struct pollfd> &polls) {
  beg = std::chrono::high_resolution_clock::now();
  polls[0].fd = -1;
}

int find_num (vector<int> &v, int num) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i] == num)
      return i;

  return -1;
}

void remove_num (vector<int> &v, int num) {
  size_t i;

  for (i = 0; i < v.size(); i++) 
    if (v[i] == num)
      break;

  for (size_t j = i; j < v.size() - 1; j++)
    v[j] = v[j + 1];

  v.pop_back();
}

void print_package_error (string s, sockaddr_in address) {
  cerr << "[PCKG ERROR]  Skipping invalid package from " << inet_ntoa(address.sin_addr) 
    << ":" << ntohs(address.sin_port) << ". " << s << endl;
} 

bool process_message (message m, map<string, vector<int>> &active_seq, struct sockaddr_in address) {
  if (m.type == bad) 
    return false;

  string content = m.cmd;

  if (m.cmd == "NO_WAY")
    m.cmd = "CAN_ADD"; 

  int pos = find_num(active_seq[m.cmd], m.cmd_seq);
  if (pos == -1) {
    print_package_error("Invalid cmd_seq.", address);
    return false;
  }
  else if (m.cmd != "GOOD_DAY" && m.cmd != "MY_LIST") 
    remove_num(active_seq[m.cmd], pos);

  if ((m.type == smpl && m.size < 18) || (m.type == cmplx && m.size < 26))
    return false;

  m.cmd = content;
  return true; 
}

void add_files (string data, map<string, string> &file_list, string sin_addr) {
  if (data == "")
    return;

  size_t enter = data.find('\n');

  while (enter != string::npos) {
    string file = data.substr(0, enter);
    cout << file << " (" << sin_addr << ")" << endl;
    file_list.insert(make_pair(file, sin_addr));
    data = data.substr(enter + 1);
    enter = data.find('\n');
  }

  cout << data << " (" << sin_addr << ")" << endl;
  file_list.insert(make_pair(data, sin_addr));
}

ssize_t check_file_size (string path) {
  struct stat s;
    
  if (stat(path.c_str(), &s) == 0) 
      if (S_ISREG(s.st_mode))
          return s.st_size;

  return -1;
}

void print_error (struct sockaddr_in server_adress, string file_name, string what, string reason) {
  int port = ntohs(server_adress.sin_port);
  string addr = inet_ntoa(server_adress.sin_addr);

  cerr << "File " << file_name << " " << what << " failed (" << addr << ":" << port 
     << " " << reason << endl;
}

int TCP_connect (int port, struct sockaddr_in server_adress, string file_name, string what) {
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (sock < 0)
    syserr("socket");

  server_adress.sin_port = htons(port);

  if (connect(sock, (struct sockaddr *)&server_adress, sizeof(server_adress)) < 0) {
    print_error(server_adress, file_name, what, "Error in connect");
    return -1;
  }

  return sock;
}

void remove_descriptor (vector <struct pollfd> &polls, vector <FILE *> &opened_files, size_t *pos) {
  if (close(polls[*pos].fd) < 0)
    syserr("close");
  
  if (fclose(opened_files[*pos - 2]) == EOF)
    syserr("fclose");

  for (size_t j = *pos; j < polls.size() - 1; j++)
    polls[j] = polls[j + 1];

  for (size_t j = *pos - 2; j < opened_files.size() - 1; j++)
    opened_files[j] = opened_files[j + 1];

  polls.pop_back();
  opened_files.pop_back();
  (*pos)--;
}

void print_log (map<int, pair<string, sockaddr_in>> &log_helper, int fd, string what) {
  string name = log_helper[fd].first;
  sockaddr_in server_adress = log_helper[fd].second;
  string sin_addr = inet_ntoa(server_adress.sin_addr);
  int port = ntohs(server_adress.sin_port);

  cout << "File " << name << " " << what << " (" << sin_addr << ":" << port
    << ")" << endl;
}

void print_error_log (map<int, pair<string, sockaddr_in>> &log_helper, int fd, string what, string reason) {
  string name = log_helper[fd].first;
  sockaddr_in server_adress = log_helper[fd].second;
  string sin_addr = inet_ntoa(server_adress.sin_addr);
  int port = ntohs(server_adress.sin_port);

  cout << "File " << name << " " << what << " failed (" << sin_addr  << ":" << port 
    << ") " << reason << endl;
}

void handle_upload_error (map<int, pair<string, sockaddr_in>> &log_helper, string reason,
      vector<struct pollfd> &polls, vector <FILE *> &opened_files, size_t *pos,
      map <string, vector<pair <string, int>>> &upload_candidates) {

  print_error_log(log_helper, polls[*pos].fd, "uploading", reason);
  string file_name = log_helper[polls[*pos].fd].first;
  upload_candidates.erase(file_name);
  log_helper.erase(polls[*pos].fd);
  remove_descriptor(polls, opened_files, pos);
}

void handle_download_error (map<int, pair<string, sockaddr_in>> &log_helper, string reason,
      vector<struct pollfd> &polls, vector <FILE *> &opened_files, size_t *pos) {

  print_error_log(log_helper, polls[*pos].fd, "downloading", reason);
  log_helper.erase(polls[*pos].fd);
  remove_descriptor(polls, opened_files, pos);
}

void find_files (string folder, set<string> &files, uint64_t &space) {
  DIR *d;
  struct dirent *dir;
  d = opendir(folder.c_str());

  if (d == NULL)
    fatal("error in opening directory");

  while ((dir = readdir(d)) != NULL) {
    struct stat s;
    string path = folder + "/" + dir->d_name;
    string file_name(dir->d_name);
    if (stat(path.c_str(), &s) == 0 && S_ISREG(s.st_mode) && (size_t)s.st_size <= space) {
      files.insert(dir->d_name);
      space -= s.st_size;
    }
  }

  closedir(d); 
}

void start_server_timer (server_timer &timer) {
  timer.is_set = true;
  timer.beg = std::chrono::high_resolution_clock::now();
  timer.end = std::chrono::high_resolution_clock::now();
}

bool check_timeout (server_timer &timer, int timeout) {
  timer.beg = std::chrono::high_resolution_clock::now();

  chrono::duration<double> diff = timer.end - timer.beg;
  if (diff.count() > timeout) 
    return false;

  return true;
}

double get_min_time (vector<struct pollfd> &polls, int timeout, map<int, server_timer> &tcp_timres) {
  double min = -1;

  for (size_t i = 1; i < polls.size(); i++) {
    if (tcp_timres.find(polls[i].fd) == tcp_timres.end())
      continue;

    server_timer timer = tcp_timres[polls[i].fd];
    if (timer.is_set) {
      chrono::duration<double> diff = timer.end - timer.beg;
      double time_left = timeout - diff.count();
      if (min == -1 || time_left < min)
        min = time_left;
    }
  }

  return min;
}

void remove_sock (vector<struct pollfd> &polls, size_t *pos) {
  if (close(polls[*pos].fd) < 0)
    cerr << "Error in closing socket." << endl;

  for (size_t j = *pos; j < polls.size() - 1; j++)
    polls[j] = polls[j + 1];

  polls.pop_back();
  
  (*pos)--;
}

string convert_file_list (set<string> &files, set<string>::iterator &it, size_t MTU) {
  size_t free_space = MTU - 18;

  string res = "";

  while (it != files.end()) {
    if ((*it).size() > free_space)
      break;

    free_space -= ((*it).size() + 1);
    res += *it + "\n";
    it++;
  }

  if (res != "")
    res.pop_back();

  return res;
}

void filter_files (set<string> &good_files, set<string> &files, string pattern) {
  good_files.clear();

  for (auto it = files.begin(); it != files.end(); it++) {
    string file_name = *it;
    if (file_name.find(pattern) != string::npos)
      good_files.insert(file_name);
  }
}

bool check_file (set<string> &files, set<string> &uploading_files, string file_name, uint64_t &space, uint64_t size) {
  if (file_name == "" || file_name.find('/') != string::npos) 
    return false;

  auto it = files.find(file_name);
  if (it != files.end()) 
    return false;
  
  it = uploading_files.find(file_name);
  
  if (it != uploading_files.end())
    return false;
  

  if (space < size)
    return false;

  space -= size;
  return true;
}

int tcp_listen (struct sockaddr_in &server_address) {
  int sock = socket(PF_INET, SOCK_STREAM, 0);
  
  if (sock < 0) {
    cerr << "unable to open socket" << endl;
    return -1;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(0);

  if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
    cerr << "error in bind" << endl;
    close(sock);
    return -1;
  }

  socklen_t server_address_len = (socklen_t) sizeof(server_address);

  if (getsockname(sock, (struct sockaddr *)&server_address, &server_address_len) == -1) {
    cerr << "error in getsockname" << endl;
    close(sock);
    return -1;
  }

  if (listen(sock, SOMAXCONN) < 0) {
    cerr << "error in listen" << endl;
    close(sock);
    return -1;
  }

  return sock;
}

void print_error_no_address (string file_name, string what, string reason) {
  cout << "File " << file_name << " " << what << " failed (:) " << reason << endl;
}
