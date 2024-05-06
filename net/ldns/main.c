/*
 * (c) NLnet Labs, 2005
 * (c) Roberto Javier Godoy, 2020, 2024
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of NLnetLabs nor the names of its
 *     contributors may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "dns_server.h"

#ifdef MULTI_PRIMARY
#include "git/common.h"
#endif

#define INBUF_SIZE 4096

opts_struct opts={0};
ldns_zone **zones;

int udp_sock;

static void start_dns_server(struct in_addr my_address, int port);
static int bind_port(int sock, int port, in_addr_t maddr);
static void* listen_udp(void* psock);
static void* listen_tcp(void* psock);

static void print_help(char *argv[]) {
    fprintf(stderr,"Use: %s", argv[0]);
    #ifdef MULTI_PRIMARY
    fprintf(stderr," --repo=<url>");
    fprintf(stderr," --branch=<name>\n");
    #endif
    fprintf(stderr," --dir=<dir>\n");
    exit(1);
}

static void parse_opts(int argc, char *argv[], opts_struct *opts) {
    opts->branch = "master";
    opts->dir = "/etc/dns";

    struct option longopts[] = {
        { "help", false, NULL, 'h' },
        { "repo", true, NULL, 1 },
        { "branch", true, NULL, 2 },
        { "dir", true, NULL, 3 },
        { "address", true, NULL, 4},
        { 0, 0, 0, 0}};

    while (true) {
        int longindex;
        const int opt = getopt_long(argc, argv, "h", longopts, &longindex);
        if (opt==-1) break;
        if (!longindex) print_help(argv);
        fprintf(stderr, "%s=%s\n", longopts[longindex].name, optarg);
        
        switch (opt) {
        case 1:
            opts->repo = optarg;
            break;
        case 2:
            opts->branch = optarg;
            break;
        case 3:
            opts->dir = optarg;
            break;
        case 4: {
            struct in_addr addr;
            if (!inet_aton(optarg, &addr)) {
               fprintf(stderr, "Invalid --address %s\n", optarg);
               exit(1);
            }
            opts->address=addr.s_addr;
          }
        }
    }

    DIR *d = opendir(opts->dir);
    if (d==NULL) {
      fprintf(stderr,"%s does not exist\n", opts->dir);
      exit(1);
    }
    closedir(d);

    #ifdef MULTI_PRIMARY
    if (!opts->address) {
       fprintf(stderr, "No --address specified\n", optarg);
       exit(1);
    }
    #endif
}

#include <signal.h>
extern pthread_rwlock_t lock;
void  INThandler(int sig)  {
    pthread_rwlock_wrlock(&lock);
    for (ldns_zone** z = zones; *z; ++z) {
        ldns_zone_deep_free(*z);
    *z = NULL;
    }   
    pthread_rwlock_unlock(&lock);
    exit(0);
}

void main(int argc, char* argv[]) {
    signal(SIGINT, INThandler);
    zones = LDNS_CALLOC(ldns_zone*,1);

    #ifdef MULTI_PRIMARY
    git_libgit2_init();
    #endif

    parse_opts(argc, argv, &opts);

    struct in_addr dns_address = {0};
    int dns_port = 53;

    #ifdef MULTI_PRIMARY
    zone_pull();
    #else 
    DIR *d = opendir(opts.dir);
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        const char* name = dir->d_name;
        if (!is_zone_file(name)) continue;
        printf("%s\n", dir->d_name);
        filename_t filename;
        set_filename(filename, opts.dir, dir->d_name);
        zone_add(zone_read(filename));
    }
    closedir(d);
    #endif

    start_dns_server(dns_address, dns_port); 

}

static void start_dns_server(struct in_addr my_address, int port) {

    printf("Listening on port %d\n", port);

    udp_sock =  socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        fprintf(stderr, "socket(): %s\n", strerror(errno));
        exit(1);
    }

    int tcp_sock =  socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        fprintf(stderr, "socket(): %s\n", strerror(errno));
        exit(1);
    }

    if (setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) <0) {
        fprintf(stderr, "setsockopt(SO_REUSEADDR): %s\n", strerror(errno));
        exit(1);
    }

    if (bind_port(udp_sock, port, my_address.s_addr)) {
        fprintf(stderr, "cannot bind(): %s\n", strerror(errno));
        exit(errno);
    }

    if (bind_port(tcp_sock, port, my_address.s_addr)) {
        fprintf(stderr, "cannot bind(): %s\n", strerror(errno));
        exit(errno);
    }

    if (listen(tcp_sock, 5) < 0) {
        fprintf(stderr, "listen(): %s\n", strerror(errno));
        exit(1);
    }

    pthread_t tcp_thread;
    pthread_create(&tcp_thread, NULL, listen_tcp, &tcp_sock);
    listen_udp(&udp_sock);
}

static int bind_port(int sock, int port, in_addr_t maddr) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = (in_port_t) htons((uint16_t)port);
    addr.sin_addr.s_addr = maddr;
    return bind(sock, (struct sockaddr *)&addr, (socklen_t) sizeof(addr));
}

static void* listen_udp(void* psock) {
    int sock = *(int*)psock;
    while (1) {
        uint8_t inbuf[INBUF_SIZE];

        struct sockaddr addr_him;
        socklen_t hislen = (socklen_t) sizeof(addr_him);

        ssize_t nb = recvfrom(sock, (void*)inbuf, INBUF_SIZE, 0, &addr_him, &hislen);

        if (nb < 1) {
            fprintf(stderr, "recvfrom(): %s\n", strerror(errno));
            exit(1);
        }

        uint8_t *outbuf=NULL;
        size_t answer_size;
        handle_dns_wire(inbuf,nb,&outbuf,&answer_size,0);

        if (outbuf) {
            (void) sendto(sock, (void*)outbuf, answer_size, 0, &addr_him, hislen);
            LDNS_FREE(outbuf);
        }
    }
}

static void* listen_tcp(void* psock) {
    int sock = *(int*)psock;
    while (1) {
        int fd = accept(sock, NULL,NULL);
        if (fd < 0) {
            fprintf(stderr, "accept(): %s\n", strerror(errno));
            exit(1);
        }

        while (1) {
          size_t nb; 
          struct timeval timeout = {1};
          uint8_t *inbuf = ldns_tcp_read_wire_timeout(fd, &nb, timeout);
          if (!inbuf) break; 
          uint8_t *outbuf=NULL;
          size_t answer_size;
          handle_dns_wire(inbuf,nb,&outbuf,&answer_size,fd);

          LDNS_FREE(inbuf);
          if (outbuf) {
              uint16_t size = htons(answer_size);
              struct iovec iov[2] = {{&size,2}, {outbuf, answer_size}};
              writev(fd, iov, 2);
              LDNS_FREE(outbuf);
          }
        }

        close(fd);
    }
}

