#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// NOTE:
// this needs to be compiled, before compile the program, change the server names
// on *servers[] compile with: "gcc -o pacemon pacemon.c -lpthread"
// usually should be executed on node1; needs ssh access without password
//

// ##############################################################################
// pacemon
// follow pacemaker.log from all cluster nodes in real time in a humam readble
// format. impossible to read the pacemaker.log? not anymore. that easy.
// ##############################################################################
//
// main accepts the pacemaker log filename because it can be another then it
// create a thread for each server
//
// pacemaker log file has loads of info; we will filter that and print out what
// it matters;
//
// use the install.sh script to install this as a service to see the log use:
// journalctl -f -u pacemon
//
// you can also redirect the output of this service to a file and "tail -f
// file.txt" pacemon can also run as an executable without the service
//
// program creates an ssh connection to the servers, so you can also run this
// program from another server, as long as you set their names on /etc/hosts
// and create all ssh connections without password
//
// Example of pacemon execution, watch the operation probe, monitor, start, stop
// and even for virtual IP:
//
// ./pacemon
// Sep 14 20:21:01 - PID: 1065 - lapps-n2 - probe        - kafdrop-ip       - lapps-n2 - not running 
// Sep 14 21:22:05 - PID: 1252 - lapps-n1 - monitor      - kafdrop          - lapps-n1 - Cancelled 
// Sep 14 21:22:07 - PID: 1252 - lapps-n1 - stop         - kafdrop          - lapps-n1 - ok 
// Sep 14 21:22:07 - PID: 1252 - lapps-n1 - monitor      - kafdrop-ip       - lapps-n1 - Cancelled 
// Sep 14 21:22:07 - PID: 1252 - lapps-n1 - stop         - kafdrop-ip       - lapps-n1 - ok 
// Sep 14 21:22:06 - PID: 1084 - lapps-n3 - start        - kafdrop-ip       - lapps-n3 - ok 
// Sep 14 21:22:07 - PID: 1084 - lapps-n3 - monitor      - kafdrop-ip       - lapps-n3 - ok 
// Sep 14 21:22:09 - PID: 1084 - lapps-n3 - start        - kafdrop          - lapps-n3 - ok 
// Sep 14 21:22:09 - PID: 1084 - lapps-n3 - monitor      - kafdrop          - lapps-n3 - ok 
// Sep 14 21:22:11 - 192.168.248.45 for virtual IP kafdrop-ip on device ens160
// 

#define MAX_PATH_LEN 1024

typedef struct {
  char server[128];
  char log_file_path[MAX_PATH_LEN];
} MonitorArgs;

// Replace with your node hostnames
const char *servers[] = {"lapps-n1", "lapps-n2", "lapps-n3", "lapps-n4"};
int num_servers = sizeof(servers) / sizeof(servers[0]);

// this is the default, you can pass as parameter another log
const char *log_file_path = "/var/log/pacemaker/pacemaker.log";

//
// process_log_executor_event will extract info from each line read
//
void process_log_executor_event(const char *line) {
  printf("%.15s", line);

  const char *pidStart = strchr(line, '[');
  const char *pidEnd = strchr(line, ']');
  if (pidStart && pidEnd && pidEnd > pidStart) {
    printf(" - PID: %.*s", (int)(pidEnd - pidStart - 1), pidStart + 1);
  }

  const char *nextWordPos = line + 16;
  while (*nextWordPos == ' ')
    nextWordPos++;
  const char *end = nextWordPos;
  while (*end && *end != ' ')
    end++;
  printf(" - %.*s", (int)(end - nextWordPos), nextWordPos);

  const char *resPos = strstr(line, "Result of ");
  if (resPos) {
    resPos += strlen("Result of ");
    const char *end = resPos;
    while (*end && *end != ' ')
      end++;
    printf(" - %-12.*s", (int)(end - resPos), resPos);
  }

  const char *opPos = strstr(line, "operation for ");
  if (opPos) {
    opPos += strlen("operation for ");
    const char *end = opPos;
    while (*end && *end != ' ')
      end++;
    printf(" - %-16.*s", (int)(end - opPos), opPos);
  }

  const char *onPos = strstr(line, " on ");
  if (onPos) {
    onPos += strlen(" on ");
    const char *end = onPos;
    while (*end && *end != ' ')
      end++;
    int length = (int)(end - onPos);
    if (length > 0 && onPos[length - 1] == ':') {
      length--;
    }
    printf(" - %.*s", length, onPos);
  }

  const char *pipePos = strchr(line, '|');
  if (pipePos) {
    const char *lastColon = NULL;
    for (const char *p = pipePos; p >= line; p--) {
      if (*p == ':') {
        lastColon = p;
        break;
      }
    }
    if (lastColon) {
      const char *start = lastColon + 1;
      while (*start == ' ')
        start++;
      int len = (int)(pipePos - start);
      printf(" - %.*s\n", len, start);
    }
  }
  fflush(stdout);
}

//
// process_IPAddr2 extracts info from ip lines
//
void process_IPAddr2(const char *line) {
  printf("%.15s", line);
  const char *arping = strstr(line, "ARPING");
  if (arping) {
    arping += strlen("ARPING ");
    char ip[64] = {0};
    sscanf(arping, "%63s", ip);
    printf(" - %s", ip);
  }
  const char *svcStart = strchr(line, '(');
  const char *svcEnd = strchr(line, ')');
  if (svcStart && svcEnd && svcEnd > svcStart) {
    printf(" for virtual IP %.*s", (int)(svcEnd - svcStart - 1), svcStart + 1);
  }
  for (int i = strlen(line) - 1; i >= 0; --i) {
    if (line[i] == ' ') {
      printf(" on device %s", &line[i + 1]);
      break;
    }
  }
  fflush(stdout);
}

//
// tail_remote_log this will be a thread for each server
// and read lines from pacemaker log
//
void *tail_remote_log(void *arg) {
  MonitorArgs *params = (MonitorArgs *)arg;

  char cmd[1024];
  char line[4096];

  int first_attempt = 1;
  time_t last_msg_time = 0;

  while (1) {
    //
    // this is where it ssh into the server for the tail command
    snprintf(cmd, sizeof(cmd), "ssh %s tail -F %s", params->server,
             params->log_file_path);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
      time_t now = time(NULL);
      if (now - last_msg_time >= 30) {
        fprintf(stderr, "Failed to connect to %s. Retrying in 2 seconds...\n",
                params->server);
        last_msg_time = now;
      }
      sleep(2);
      continue;
    }

    if (!first_attempt) {
      fprintf(stderr, "Reconnected to %s\n", params->server);
    }
    first_attempt = 0;

    while (fgets(line, sizeof(line), fp)) {
      if (strstr(line, "log_executor_event"))
        process_log_executor_event(line);
      if (strstr(line, "ARPING"))
        process_IPAddr2(line);
    }

    // If we reached EOF or error, close and retry after 2 seconds
    pclose(fp);
    // fprintf(stderr, "Connection lost to %s. Retrying in 2 seconds...\n",
    // params->server);
    sleep(2);
  }

  return NULL; // well... let's hope to return nothing never
}

int main(int argc, char *argv[]) {
  pthread_t threads[16];
  MonitorArgs args[16];

  for (int i = 0; i < num_servers; ++i) {
    strncpy(args[i].server, servers[i], sizeof(args[i].server) - 1);
    args[i].server[sizeof(args[i].server) - 1] = '\0';

    strncpy(args[i].log_file_path, log_file_path,
            sizeof(args[i].log_file_path) - 1);
    args[i].log_file_path[sizeof(args[i].log_file_path) - 1] = '\0';

    pthread_create(&threads[i], NULL, tail_remote_log, &args[i]);
  }

  for (int i = 0; i < num_servers; ++i) {
    pthread_join(threads[i], NULL);
  }

  return 0;
}
