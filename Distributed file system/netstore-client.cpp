#include "helper.h"

#define BUFFSIZE 524288

using namespace std;
using namespace boost::program_options;
using namespace std::filesystem;

bool compare (const pair<string, int> &a, const pair<string, int> &b) {
  return a.second < b.second;
}

int main(int argc, const char *argv[]) {
  string MCAST_ADDR;
  int CMD_PORT;
  string OUT_FLDR;
  int TIMEOUT;
  try {
    options_description desc{"Options"};
    desc.add_options()
      ("multicast address,g", value<std::string>()->required(), "multicast address")
      ("udp port,p", value<int>()->required(), "udp port")
      ("destination folder,o", value<std::string>()->required(), "destination folder")
      ("timeout,t", value<int>()->default_value(5), "timeout");

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    MCAST_ADDR = vm["multicast address"].as<std::string>();
    CMD_PORT = vm["udp port"].as<int>();
    OUT_FLDR = vm["destination folder"].as<std::string>();
    TIMEOUT = vm["timeout"].as<int>();

  }
  catch (const error &ex) {
    cerr << "Error: " << ex.what() << endl;;
    return 1;
  }

  if (TIMEOUT > 300 || TIMEOUT < 1)
    fatal("timeout must be in interval [0, 300]");

  DIR *d;
  d = opendir(OUT_FLDR.c_str());

  if (d == NULL)
    fatal("error in opening directory");

  closedir(d);

  // deklaracje
  vector <struct pollfd> polls(2);
  int optval = 1;
  struct sockaddr_in local_address, remote_address, server_address;
  std::chrono::time_point<std::chrono::high_resolution_clock> beg, end;
  std::chrono::duration<double> diff;
  Listen_mode mode = none;
  message m;
  char buff[BUFFSIZE];
  uint64_t cmd_seq = 0;
  ssize_t rcv_len;
  vector <pair <string, int> > free_spaces;
  map <string, string> file_list;
  map <string, vector<int>> active_seq;
  map <string, vector<pair <string, int>>> upload_candidates;
  map <uint64_t, string> file_paths;
  map <string, ssize_t> file_sizes;
  map <int, pair<string, sockaddr_in>> log_helper;
  socklen_t rcva_len;
  string server_unicast;
  vector <FILE *> opened_files;
  //

  vector <int> v;
  active_seq.insert(make_pair("GOOD_DAY", v));
  active_seq.insert(make_pair("MY_LIST", v));
  active_seq.insert(make_pair("CONNECT_ME", v));
  active_seq.insert(make_pair("CAN_ADD", v));

  polls[0].fd = 0;

  polls[1].fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (polls[1].fd < 0)
    syserr("socket");

  if (setsockopt(polls[1].fd, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof optval) < 0)
    syserr("setsockopt broadcast");

  local_address.sin_family = AF_INET;
  local_address.sin_addr.s_addr = htonl(INADDR_ANY);
  local_address.sin_port = htons(0);
  if (bind(polls[1].fd, (struct sockaddr *)&local_address, sizeof local_address) < 0)
    syserr("bind");

  remote_address.sin_family = AF_INET;
  remote_address.sin_port = htons(CMD_PORT);
  if (inet_aton(MCAST_ADDR.c_str(), &remote_address.sin_addr) == 0)
    syserr("inet_aton");

  for (int i = 0; i < 2; i++) {
    polls[i].events = POLLIN;
    polls[i].revents = 0;
  }

  while (true) {
    for (size_t i = 0; i < polls.size(); i++) 
      polls[i].revents = 0;

    if (mode != none) {
      end = std::chrono::high_resolution_clock::now();
      diff = end - beg;

      if (diff.count() >= TIMEOUT) {
        if (mode == discover) {
          active_seq["GOOD_DAY"].clear();
          sort(free_spaces.begin(), free_spaces.end(), compare);
        }
        if (mode == search_list)
          active_seq["MY_LIST"].clear();
        mode = none;
        polls[0].fd = 0;
        poll (polls.data(), polls.size(), -1);
      }
      else
        poll (polls.data(), polls.size(), (TIMEOUT - diff.count()) * 1000);
    }
    else 
      poll (polls.data(), polls.size(), -1);

    if (polls[0].revents & POLLIN) {
      polls[0].revents = 0;
      string line;
      getline(cin, line);
      string line_lowercase = line;
      transform(line_lowercase.begin(), line_lowercase.end(), line_lowercase.begin(), ::tolower);
      size_t space = line.find(' ');

      if (space == string::npos && line_lowercase != "discover" && line_lowercase != "search" && line_lowercase != "exit") 
        continue;

      else if (line_lowercase == "discover") {
        m = init_smpl_message("HELLO", &cmd_seq, "");
        convert_message(buff, m);
        
        if(sendto(polls[1].fd, buff, m.size, 0, (struct sockaddr *)&remote_address, sizeof remote_address) != m.size)
          syserr("sendto");

        active_seq["GOOD_DAY"].push_back(cmd_seq);
        mode = discover;
        free_spaces.clear();
        start_timer (beg, polls);
      }

      else if (line_lowercase == "exit") {
        for (size_t i = 1; i < polls.size(); i++)
          if ((close(polls[i].fd) < 0))
            syserr("close");

        for (size_t i = 0; i < opened_files.size(); i++)
          if (fclose(opened_files[i]) == EOF)
            syserr("fclose");

        return 0;
      }

      else {
        string query = line_lowercase.substr(0, space);
        string subname = "";

        if (query == "search") {
          if (space != string::npos)
            subname = line.substr(space + 1);

          m = init_smpl_message("LIST", &cmd_seq, subname);
          convert_message(buff, m);

          if (sendto(polls[1].fd, buff, m.size, 0, (struct sockaddr *)&remote_address, sizeof remote_address) != m.size)
            syserr("sendto");

          active_seq["MY_LIST"].push_back(cmd_seq);
          mode = search_list;
          file_list.clear();
          start_timer (beg, polls);
        }

        else if (query == "fetch") {
          subname = line.substr(space + 1);

          if (file_list.find(subname) != file_list.end()) {
            m = init_smpl_message("GET", &cmd_seq, subname);
            convert_message(buff, m);
            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(CMD_PORT);
            server_unicast = file_list.find(subname)->second;

            if (inet_aton(server_unicast.c_str(), &server_address.sin_addr) == 0)
              syserr("inet_aton");

            if (sendto(polls[1].fd, buff, m.size, 0, (struct sockaddr *)&server_address, sizeof server_address) != m.size)
              syserr("sendto");

            active_seq["CONNECT_ME"].push_back(cmd_seq);
          }
          else
            print_error_no_address(subname, "downloading", 
              "File is not avaiable. Please type 'search' to see avaiable files.");      
        }

        else if (query == "upload") {
          subname = line.substr(space + 1);
          path p(subname);
          ssize_t size = check_file_size(subname);
          string file_name = p.filename();

          if (size >= 0) {
            auto it = upload_candidates.find(file_name);
            if (it == upload_candidates.end()) {
              if (free_spaces.size() > 0) {
                upload_candidates.insert(make_pair(file_name, free_spaces));
                file_paths.insert(make_pair(cmd_seq + 1, subname));
                file_sizes.insert(make_pair(file_name, size));
                server_address.sin_family = AF_INET;
                server_address.sin_port = htons(CMD_PORT);
                server_unicast = free_spaces[free_spaces.size() - 1].first;

                if (inet_aton(server_unicast.c_str(), &server_address.sin_addr) == 0)
                  syserr("inet_aton");

                m = init_cmplx_message("ADD", &cmd_seq, (uint64_t)size, p.filename());
                convert_message(buff, m);

                if (sendto(polls[1].fd, buff, m.size, 0, (struct sockaddr *)&server_address, sizeof server_address) != m.size)
                  syserr("sendto");

                active_seq["CAN_ADD"].push_back(cmd_seq);
                upload_candidates[file_name].pop_back();
              }
              else
                print_error_no_address(subname, "uploading", 
                  "No servers found. Please type 'discover' to find servers.");
            }
            
            else
              print_error_no_address(subname, "uploading", 
                  "File is already uploading.");
          }

          else 
            cout << "File " << subname << " does not exist" << endl;
        }

        else if (query == "remove") {
          subname = line.substr(space + 1);

          if (subname == "") 
            continue;

          m = init_smpl_message("DEL", &cmd_seq, subname);
          convert_message(buff, m);

          if (sendto(polls[1].fd, buff, m.size, 0, (struct sockaddr *)&remote_address, sizeof remote_address) != m.size)
            syserr("sendto");
        }
      }
    }

    if (polls[1].revents & POLLIN) {
      polls[1].revents = 0;
      rcva_len = (socklen_t) sizeof(server_address);
      rcv_len = recvfrom(polls[1].fd, buff, sizeof(buff), 0,
        (struct sockaddr *) &server_address, &rcva_len);
      if (rcv_len < 0)
        syserr("recvfrom");

      message m = get_message(buff, rcv_len, server_address);
      if (process_message(m, active_seq, server_address)) {
        server_unicast = inet_ntoa (server_address.sin_addr);
        if (m.cmd == "GOOD_DAY") {
          cout << "Found " << server_unicast << " (" << m.data 
            << ") with free space " << m.param << endl;

          free_spaces.push_back(make_pair(server_unicast, m.param));  
        }

        else if (m.cmd == "MY_LIST")
          add_files(m.data, file_list, server_unicast);

        else if (m.cmd == "NO_WAY") {
          auto it = upload_candidates.find(m.data);
          if (it != upload_candidates.end()) {
            if (upload_candidates[m.data].size() == 0) {
              cout << "File " << m.data << " too big" << endl;
              file_sizes.erase(m.data);
              file_paths.erase(m.cmd_seq);
              upload_candidates.erase(m.data);
            }

            else {
              server_unicast = upload_candidates[m.data][upload_candidates[m.data].size() -1].first;
              server_address.sin_family = AF_INET;
              server_address.sin_port = htons(CMD_PORT);
              
              if (inet_aton(server_unicast.c_str(), &server_address.sin_addr) == 0)
                  syserr("inet_aton");

              ssize_t size = file_sizes[m.data];
              string path = file_paths[m.cmd_seq];
              file_paths.erase(m.cmd_seq);

              m = init_cmplx_message("ADD", &cmd_seq, (uint64_t)size, m.data);
              convert_message(buff, m);

              if (sendto(polls[1].fd, buff, m.size, 0, (struct sockaddr *)&server_address, sizeof server_address) != m.size)
                syserr("sendto");

              file_paths.insert(make_pair(m.cmd_seq, path));
              active_seq["CAN_ADD"].push_back(cmd_seq);
              upload_candidates[m.data].pop_back();
            }
          }
          else
            print_package_error("Invalid filename in field 'data'", server_address); 
        }

        else if (m.cmd == "CONNECT_ME") {
          struct pollfd poll;
          poll.fd = TCP_connect (m.param, server_address, m.data, "downloading");

          if (poll.fd == -1)
            continue;

          poll.events = POLLIN;
          poll.revents = 0;
          polls.push_back(poll);
          log_helper.insert(make_pair(poll.fd, make_pair(m.data, server_address)));

          string path = OUT_FLDR + "/" + m.data;
          FILE *f = fopen(path.c_str(), "w");

          if (f == NULL)
            syserr("unable to open file");

          opened_files.push_back(f);   
        }

        else if (m.cmd == "CAN_ADD") {
          string root = file_paths[m.cmd_seq];
          file_paths.erase(m.cmd_seq);
          path p(root);
          string file_name = p.filename();
          struct pollfd poll;
          poll.fd = TCP_connect (m.param, server_address, file_name, "uploading");

          if (poll.fd == -1)
            continue;

          poll.events = POLLOUT;
          poll.revents = 0;
          FILE *f = fopen(root.c_str(), "r");
          log_helper.insert(make_pair(poll.fd, make_pair(file_name, server_address)));

          if (f == NULL) {
            print_error_log(log_helper, poll.fd, "uploading", "unable to open file");
            log_helper.erase(poll.fd);
            continue;
          }
          
          polls.push_back(poll);
          opened_files.push_back(f);
        }
      }
    }

    for (size_t i = 2; i < polls.size(); i++) {
      if (polls[i].revents & POLLIN) {
        polls[i].revents = 0;
        rcv_len = read(polls[i].fd, buff, BUFFSIZE);

        if (rcv_len < 0) 
          handle_download_error(log_helper, "error in read from socket", polls, opened_files, &i);

        else if (rcv_len == 0) {
          print_log(log_helper, polls[i].fd, "downloaded");
          log_helper.erase(polls[i].fd);
          remove_descriptor(polls, opened_files, &i);     
        }

        else
          if (fwrite(buff, sizeof(char), rcv_len, opened_files[i - 2]) < (size_t)rcv_len)
            handle_download_error(log_helper, "error in write to file", polls, opened_files, &i);
      }

      if (polls[i].revents & POLLOUT) {
        polls[i].revents = 0;
        rcv_len = fread(buff, sizeof(char), BUFFSIZE, opened_files[i - 2]);
        
        if (ferror(opened_files[i - 2]))
          handle_upload_error(log_helper, "error in reading from file", polls,
              opened_files, &i, upload_candidates);

        else if (feof(opened_files[i - 2])) {
          if (write(polls[i].fd, buff, rcv_len) < rcv_len)
            handle_upload_error(log_helper, "error in write to socket", polls,
              opened_files, &i, upload_candidates);

          else {
            print_log(log_helper, polls[i].fd, "uploaded");
            string file_name = log_helper[polls[i].fd].first;
            upload_candidates.erase(file_name);
            log_helper.erase(polls[i].fd);
            remove_descriptor(polls, opened_files, &i);
          }
        }

        else
          if (write(polls[i].fd, buff, rcv_len) < (ssize_t)rcv_len)
            handle_upload_error(log_helper, "error in write to socket", polls,
              opened_files, &i, upload_candidates);
      }
    }
  }
}
