#include "helper.h"

using namespace std;
using namespace boost::program_options;

#define BUFFSIZE 524288
#define MAX_UDP_SIZE 500

vector <struct pollfd> polls(1);
map <int, FILE*> opened_files;

void sig_handler (int sig_num) {
  for (size_t i = 0; i < polls.size(); i++) {
    if (opened_files.find(polls[i].fd) != opened_files.end()) {
      FILE * f = opened_files[polls[i].fd];

      if (fclose(f) < 0)
        cerr << "Error in closing file" << endl;
    }

    if (close(polls[i].fd) < 0)
      cerr << "Error in closing socket" << endl;
  }

  exit(sig_num);
}


int main (int argc, const char *argv[]) {
  signal(SIGINT, sig_handler);

  string MCAST_ADDR;
  int CMD_PORT;
  uint64_t MAX_SPACE;
  string SHRD_FLDR;
  int TIMEOUT;

  try {
    options_description desc{"Options"};
    desc.add_options()
      ("multicast address,g", value<std::string>()->required(), "multicast address")
      ("udp port,p", value<int>()->required(), "udp port")
      ("disc space,b", value<uint64_t>()->default_value(52428800), "disc space")
      ("shared folder,f", value<std::string>()->required(), "shared folder")
      ("timeout,t", value<int>()->default_value(5), "timeout");

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    MCAST_ADDR = vm["multicast address"].as<std::string>();
    CMD_PORT = vm["udp port"].as<int>();
    MAX_SPACE = vm["disc space"].as<uint64_t>();
    SHRD_FLDR = vm["shared folder"].as<std::string>();
    TIMEOUT = vm["timeout"].as<int>();

  }
  catch (const error &ex) {
    cerr << "Error: " << ex.what() << endl;
    return 1;
  }

  if (TIMEOUT > 300 || TIMEOUT < 1)
    fatal("timeout must be in interval [0, 300]");

  if (CMD_PORT > 65535 || CMD_PORT < 0)
    fatal("invalid port number");

  // deklaracje
  char buff[BUFFSIZE];
  struct ip_mreq ip_mreq;
  struct sockaddr_in server_address, local_address, client_address;
  socklen_t client_address_len = (socklen_t) sizeof(client_address);
  map <int, server_timer> tcp_timers;
  map <int, struct sockaddr_in> client_addresses;
  map <int, pair<string, Server_mode>> file_waiters;
  map <int, FILE*> opened_files; 
  double min_time;
  int sock, port;
  string sin_addr;
  string file_name;
  ssize_t rcv_len, size;
  vector <struct pollfd> polls(1);
  set <string> my_files;
  set <string> uploading_files;
  set <string> good_files;
  uint64_t cmd_seq, file_size;
  Server_mode mode;
  //

  find_files(SHRD_FLDR, my_files, MAX_SPACE);

  polls[0].fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (polls[0].fd < 0)
    syserr("socket");

  ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (inet_aton(MCAST_ADDR.c_str(), &ip_mreq.imr_multiaddr) == 0)
    syserr("inet_aton");
  if (setsockopt(polls[0].fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof ip_mreq) < 0)
    syserr("setsockopt");

  local_address.sin_family = AF_INET;
  local_address.sin_addr.s_addr = htonl(INADDR_ANY);
  local_address.sin_port = htons(CMD_PORT);
  if (bind(polls[0].fd, (struct sockaddr *)&local_address, sizeof local_address) < 0)
    syserr("bind");

  polls[0].events = POLLIN;
  polls[0].revents = 0;

  while (true) {
    min_time = get_min_time(polls, TIMEOUT, tcp_timers);

    for (size_t i = 0; i < polls.size(); i++)
      polls[i].revents = 0;

    poll(polls.data(), polls.size(), (int)(min_time * 1000));

    for (size_t i = 0; i < polls.size(); i++) {
      if (i == 0 && polls[i].revents & POLLIN) {
        
        rcv_len = recvfrom(polls[0].fd, buff, sizeof(buff), 0,
          (struct sockaddr *) &client_address, &client_address_len);
        
        if (rcv_len < 0) {
          cerr << "read from client failed" << endl;
          continue;
        }

        message m = get_message(buff, rcv_len, client_address);
        if (m.type == bad)
          continue;

        if (m.cmd == "HELLO") {
          cmd_seq = m.cmd_seq - 1;
          m = init_cmplx_message("GOOD_DAY", &cmd_seq, MAX_SPACE, MCAST_ADDR);
          convert_message(buff, m);

          if (sendto(polls[0].fd, buff, m.size, 0, 
            (struct sockaddr *) &client_address, client_address_len) != m.size) {
            cerr << "send to client failed" << endl;
            continue;  
          }
        }
        else if (m.cmd == "LIST") {
          filter_files(good_files, my_files, m.data);
          auto it = good_files.begin();

          while (it != good_files.end()) {
            string data = convert_file_list(good_files, it, MAX_UDP_SIZE);
            cmd_seq = m.cmd_seq - 1;

            m = init_smpl_message("MY_LIST", &cmd_seq, data);
            convert_message(buff, m);

            if (sendto(polls[0].fd, buff, m.size, 0, 
              (struct sockaddr *) &client_address, client_address_len) != m.size) {
              cerr << "send to client failed" << endl;
              continue;  
            }  
          }
        }
        else if (m.cmd == "DEL") {
          auto it = my_files.find(m.data);
          if (it != my_files.end()) {
            string path = SHRD_FLDR + "/" + *it;
            size = check_file_size(path);
            if (size > 0)
              MAX_SPACE += size;

            my_files.erase(m.data);
            if (remove(path.c_str()) != 0)
              cerr << "Unable to delete file " << m.data << endl;
          }
        }      
        else if (m.cmd == "GET") {
          file_name = m.data;
          cmd_seq = m.cmd_seq - 1;
          string log = "No file " + file_name + " on server.";

          if (my_files.find(file_name) == my_files.end()) {
            print_package_error(log, client_address);
            continue;
          }

          size = check_file_size(SHRD_FLDR + "/" + file_name);

          if (size < 0) {
            print_package_error(log, client_address);
            continue;
          }
          
          sock = tcp_listen(server_address);

          if (sock < 0)
            continue;

          fcntl(sock, F_SETFL, O_NONBLOCK);
          port = ntohs(server_address.sin_port);
          struct pollfd poll;
          poll.fd = sock;
          polls.push_back(poll);
          server_timer timer;
          start_server_timer(timer);
          tcp_timers.insert(make_pair(sock, timer));
          client_addresses.insert(make_pair(sock, client_address));
          file_waiters.insert(make_pair(sock, make_pair(file_name, get_file)));

          m = init_cmplx_message("CONNECT_ME", &cmd_seq, uint64_t(port) , file_name);
          convert_message(buff, m);

          if (sendto(polls[0].fd, buff, m.size, 0, 
            (struct sockaddr *) &client_address, client_address_len) != m.size) {
            cerr << "send to client failed" << endl;
            continue;  
          }  
        }
        else if (m.cmd == "ADD") {
          file_name = m.data;
          cmd_seq = m.cmd_seq - 1;
          file_size = m.param;

          if (!check_file (my_files, uploading_files, file_name, MAX_SPACE, file_size)) 
            m = init_smpl_message("NO_WAY", &cmd_seq, file_name);            

          else {
            sock = tcp_listen(server_address);

            if (sock < 0)
              continue;

            fcntl(sock, F_SETFL, O_NONBLOCK);
            port = ntohs(server_address.sin_port);
            struct pollfd poll;
            poll.fd = sock;
            polls.push_back(poll);
            server_timer timer;
            start_server_timer(timer);
            tcp_timers.insert(make_pair(sock, timer));
            client_addresses.insert(make_pair(sock, client_address));
            file_waiters.insert(make_pair(sock, make_pair(file_name, upload_file)));
            uploading_files.insert(file_name);

            m = init_cmplx_message("CAN_ADD", &cmd_seq, (uint64_t)port, "");
          }

          convert_message(buff, m);

          if (sendto(polls[0].fd, buff, m.size, 0, 
            (struct sockaddr *) &client_address, client_address_len) != m.size) {
            cerr << "send to client failed" << endl;
            continue;  
          } 
        }
      }

      else if (i > 0) {
        if (tcp_timers.find(polls[i].fd) != tcp_timers.end() && tcp_timers[polls[i].fd].is_set) {
          if (!check_timeout(tcp_timers[polls[i].fd], TIMEOUT)) {
            tcp_timers.erase(polls[i].fd);
            client_addresses.erase(polls[i].fd);
            file_waiters.erase(polls[i].fd);
            remove_sock(polls, &i);  
          }
          
          else {
            client_address = client_addresses[polls[i].fd];
            sock = accept(polls[i].fd, (struct sockaddr *)&client_addresses, &client_address_len);

            if (sock == -1) {
              if (errno != EAGAIN && errno != EWOULDBLOCK) {
                cerr << "Error in accept" << endl;
                tcp_timers.erase(polls[i].fd);
                client_addresses.erase(polls[i].fd);
                file_waiters.erase(polls[i].fd);
                remove_sock(polls, &i);
                continue;
              }
            }

            else {
              file_name = file_waiters[polls[i].fd].first;
              mode = file_waiters[polls[i].fd].second;
              string path = SHRD_FLDR + "/" + file_name;
              FILE *f;

              if (mode == get_file)
                f = fopen(path.c_str(), "r");
              
              else 
                f = fopen(path.c_str(), "w");

              if (f == NULL) {
                cerr << "unable to open file " << file_name << endl;
                tcp_timers.erase(polls[i].fd);
                client_addresses.erase(polls[i].fd);
                file_waiters.erase(polls[i].fd);
                remove_sock(polls, &i);
                continue;
              }

              struct pollfd poll;
              poll.fd = sock;

              if (mode == get_file) 
                poll.events = POLLOUT;
              
              else if (mode == upload_file)
                poll.events = POLLIN;

              poll.revents = 0;
              polls.push_back(poll);
              opened_files.insert(make_pair(sock, f));
              tcp_timers.erase(polls[i].fd);
              client_addresses.erase(polls[i].fd);
              file_waiters.erase(polls[i].fd);

              if (mode == upload_file)
                file_waiters.insert(make_pair(sock, make_pair(file_name, mode)));

              remove_sock(polls, &i);
            }
          }
        }

        else if (polls[i].revents & POLLIN) {
          rcv_len = read(polls[i].fd, buff, BUFFSIZE);
          FILE *f = opened_files[polls[i].fd];
          
          if (rcv_len < 0) {
            // cerr << "Error in read from socket" << endl;
            continue;
          }

          else if (rcv_len == 0) {
            opened_files.erase(polls[i].fd);
            file_name = file_waiters[polls[i].fd].first;
            file_waiters.erase(polls[i].fd);
            uploading_files.erase(file_name);
            my_files.insert(file_name);
            remove_sock(polls, &i);
            
            if (fclose(f) == EOF) {
              cerr << "Error in closing file" << endl;
              continue;
            }
          }

          else {
            if (fwrite(buff, sizeof(char), rcv_len, f) < (size_t)rcv_len) {
              cerr << "partial / failed write to file" << endl;
              continue;
            }
          }
        }

        else if (polls[i].revents & POLLOUT) {
          FILE *f = opened_files[polls[i].fd];
          rcv_len = fread(buff, sizeof(char), BUFFSIZE, f);
          
          if (ferror(f)) {
            cerr << "Error in reading from file" << endl;
            continue;
          }

          else if (feof(f)) {
            if (write(polls[i].fd, buff, rcv_len) < (ssize_t)rcv_len) {
              // cerr << "partial/ failed write to socket" << endl;
              continue;
            }

            opened_files.erase(polls[i].fd);
            remove_sock(polls, &i);
            if (fclose(f) == EOF) {
              cerr << "Error in closing file" << endl;
              continue;
            }
          }

          else {
            if (write(polls[i].fd, buff, rcv_len) < (ssize_t)rcv_len) {
              // cerr << "partial/ failed write to socket" << endl;
              continue;
            }
          }
        }
      }
    }
  }
}
