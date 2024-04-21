/*
 * ldnsd. Light-weight DNS daemon
 *
 * Tiny dns server to show how a real one could be built.
 *
 * (c) NLnet Labs, 2005
 * (c) Roberto Javier Godoy, 2020, 2024
 *
 * Copyright (c) 2005,2006, NLnetLabs
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

#include <ldns/ldns.h>

#define CAN_CREATE_ZONE
#define CAN_DELETE_ZONE

#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <errno.h>

#include <pthread.h>

#define LDNS_RCODE_ALREADY_HANDLED 255
#define INBUF_SIZE 4096

void handle_dns_wire(void* inbuf,ssize_t nb, uint8_t** outbuf, size_t *answer_size, int sock);
void handle_dns_pkt(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt, int sock);
void handle_dns_query(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt, int sock);
ldns_pkt_rcode handle_dns_update(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt);
void handle_axfr_request(ldns_zone*, ldns_pkt* answer_pkt, int sock);


static pthread_mutex_t mutex;
static pthread_mutex_t update_mutex;

static void* listen_tcp(void* psock);

static int bind_port(int sock, int port, in_addr_t maddr)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = (in_port_t) htons((uint16_t)port);
    addr.sin_addr.s_addr = maddr;
    return bind(sock, (struct sockaddr *)&addr, (socklen_t) sizeof(addr));
}


#define RRSET_CLONE 1
#define RRSET_FOLLOW_CNAME 2

static void 
get_rrset_into(const ldns_zone *zone, const ldns_rdf *owner_name, const ldns_rr_type qtype, const ldns_rr_class qclass, int flags, ldns_rr_list *rrlist)
{
    if (!zone || !owner_name) {
        fprintf(stderr, "Warning: get_rrset called with NULL zone or owner name\n");
        return;
    }
    
    for (uint16_t i = 0; i < ldns_zone_rr_count(zone); i++) {
        ldns_rr *rr = ldns_rr_list_rr(ldns_zone_rrs(zone), i);
        if (ldns_dname_compare(ldns_rr_owner(rr), owner_name) == 0 &&
            (ldns_rr_get_class(rr) == qclass ||  LDNS_RR_CLASS_ANY == qclass)) {
	    if (ldns_rr_get_type(rr) == qtype || LDNS_RR_TYPE_ANY == qtype) {
              if (flags&RRSET_CLONE) rr = ldns_rr_clone(rr);
              ldns_rr_list_push_rr(rrlist, rr);
            } else if ((flags&RRSET_FOLLOW_CNAME) && ldns_rr_get_type(rr)==LDNS_RR_TYPE_CNAME) {
              if (flags&RRSET_CLONE) rr = ldns_rr_clone(rr);
              ldns_rr_list_push_rr(rrlist, rr);
              if (ldns_rr_list_rr_count(rrlist)<20) {
                get_rrset_into(zone, ldns_rr_rdf(rr,0), qtype, qclass, flags, rrlist);
              }
              return;
            }
        }
    }
}

static ldns_rr_list *
get_rrset(const ldns_zone *zone, const ldns_rdf *owner_name, const ldns_rr_type qtype, const ldns_rr_class qclass, int flags)
{
    ldns_rr_list *rrlist = ldns_rr_list_new();
    get_rrset_into(zone, owner_name, qtype, qclass, flags, rrlist);
    return rrlist;
}

bool test_rrset(const ldns_zone *zone, const ldns_rr *rr, const ldns_rr_type qtype, const ldns_rr_class qclass)
{
   if (!zone) return false;
   ldns_rr_list* rr_list = get_rrset(zone, ldns_rr_owner(rr), qtype, qclass, 0);
   size_t count = ldns_rr_list_rr_count(rr_list);
   ldns_rr_list_free(rr_list);
   return count>0;
}

void start_dns_server(struct in_addr my_address, int port) {

    printf("Listening on port %d\n", port);
    int udp_sock =  socket(AF_INET, SOCK_DGRAM, 0);
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

    /* Done. Now receive */
    pthread_t tcp_thread;
    pthread_create(&tcp_thread, NULL, listen_tcp, &tcp_sock);

    while (1) {
        uint8_t inbuf[INBUF_SIZE];

        struct sockaddr addr_him;
        socklen_t hislen = (socklen_t) sizeof(addr_him);

        ssize_t nb = recvfrom(udp_sock, (void*)inbuf, INBUF_SIZE, 0, &addr_him, &hislen);
		
        if (nb < 1) {
            fprintf(stderr, "recvfrom(): %s\n", strerror(errno));
            exit(1);
        }
        
        uint8_t *outbuf=NULL;
        size_t answer_size;
        handle_dns_wire(inbuf,nb,&outbuf,&answer_size,0);

        if (outbuf) {
            (void) sendto(udp_sock, (void*)outbuf, answer_size, 0, &addr_him, hislen);
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

        size_t nb; 
        struct timeval timeout = {1};
        uint8_t *inbuf = ldns_tcp_read_wire_timeout(fd, &nb, timeout);
        uint8_t *outbuf=NULL;
        size_t answer_size;
        handle_dns_wire(inbuf,nb,&outbuf,&answer_size,fd);

        if (outbuf) {
            uint16_t size = htons(answer_size);
            struct iovec iov[2] = {{&size,2}, {outbuf, answer_size}};
            writev(fd, iov, 2);
            LDNS_FREE(outbuf);        
        }
          
        LDNS_FREE(inbuf);
    }
}

void handle_dns_wire(void* inbuf,ssize_t nb,uint8_t** outbuf, size_t *answer_size, int sock) {
        printf("Got query of %u bytes\n", (unsigned int) nb);

        ldns_status status;
        ldns_pkt *query_pkt;
        ldns_pkt *answer_pkt;

        *outbuf = NULL;

        status = ldns_wire2pkt(&query_pkt, inbuf, (size_t) nb);
        if (status != LDNS_STATUS_OK) {
            printf("Got bad packet: %s\n", ldns_get_errorstr_by_id(status));            
            return;
        } 
	ldns_pkt_print(stdout, query_pkt);
        

        if (ldns_pkt_qr(query_pkt)) {
            printf("Received DNS response\n");
            ldns_pkt_free(query_pkt);
            return;
        }
        
        answer_pkt = ldns_pkt_new();

        ldns_pkt_set_opcode(answer_pkt, ldns_pkt_get_opcode(query_pkt));
        ldns_pkt_set_id(answer_pkt, ldns_pkt_id(query_pkt));
        ldns_pkt_set_qr(answer_pkt, 1);

        if (ldns_pkt_edns(query_pkt)) {
          ldns_pkt_set_edns_udp_size(answer_pkt, 4096);
          if (ldns_pkt_edns_version(query_pkt)>0) {
            ldns_pkt_set_edns_extended_rcode(answer_pkt, 1);
          }
          //ldns_edns_option_list *edns_opts = ldns_edns_option_list_new();
          //ldns_edns_option *nsid = ldns_edns_new_from_data(LDNS_EDNS_NSID,2,"\x10");
          //ldns_edns_option_list_push(edns_opts, nsid);
          //ldns_pkt_set_edns_option_list(answer_pkt,edns_opts);
        }            
        
        if (!ldns_pkt_get_rcode(answer_pkt) && !ldns_pkt_edns_extended_rcode(answer_pkt)) {
          ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_NOTIMPL);
          handle_dns_pkt(query_pkt, answer_pkt, sock);
        }
        
        ldns_pkt_free(query_pkt);
        if (ldns_pkt_get_rcode(answer_pkt)==LDNS_RCODE_ALREADY_HANDLED) {
           ldns_pkt_free(answer_pkt);
            return;
        } 
        status = ldns_pkt2wire(outbuf, answer_pkt, answer_size);
        
        if (status != LDNS_STATUS_OK) {
            printf("Error creating answer: %s\n", ldns_get_errorstr_by_id(status));
            LDNS_FREE(outbuf);        
        } else {
            ldns_pkt_print(stdout, answer_pkt);            
        }
        ldns_pkt_free(answer_pkt);

}


static ldns_zone **zones;

ldns_zone* zone_find(ldns_rdf* name, ldns_rr_class zclass) {
    for (ldns_zone** z = zones; *z; ++z) {
        ldns_rr *soa = ldns_zone_soa(*z);
        if (ldns_rr_get_class(soa)==zclass && !ldns_dname_compare(ldns_rr_owner(soa), name)) return *z;
    } 
    name = ldns_dname_left_chop(name);
    if (name) {
      ldns_zone* zone=zone_find(name, zclass);
      ldns_rdf_deep_free(name);
      return zone;
    } else {
      return NULL;
    }
}

void zone_add(ldns_zone* zone) {
    size_t count = 0;
    for (ldns_zone** z = zones; *z; ++z) count++; 
    zones = LDNS_XREALLOC(zones, ldns_zone*, count+2);
    zones[count]=zone;
    zones[count+1]=NULL;
}

void zone_del(ldns_zone* zone) {
    size_t count = 0;
    for (ldns_zone** z = zones; *z; ++z) count++; 
    for (ldns_zone** z = zones; *z; ++z) {
      if (*z==zone) {
        ldns_zone_deep_free(zone);
        zones[count-1]=NULL;
        return;
      }
    }
}

bool del_rr_data(ldns_zone* zone, ldns_rr* rr) {
   ldns_rr_list* rrs = ldns_zone_rrs(zone);
   size_t count = ldns_rr_list_rr_count(rrs);
   for (size_t i=0;i<count;i++) {
     if (ldns_rr_compare(rr, ldns_rr_list_rr(rrs,i))==0) {
       fprintf(stderr, "Delete RR\n");
       ldns_rr_list_set_rr(rrs,ldns_rr_list_rr(rrs,count-1),i);
       ldns_rr_list_set_rr_count(rrs,count-1);
       ldns_rr_free(rr);
       return true;
     }
   }
   return false;
}

bool del_rr(ldns_zone* zone, ldns_rdf* name, ldns_rr_type type) {
   bool result = false;
   ldns_rr_list* rrs = ldns_zone_rrs(zone);
   size_t count = ldns_rr_list_rr_count(rrs);
   for (size_t i=0;i<count;i++) {
     ldns_rr *rr = ldns_rr_list_rr(rrs,i);
     if ((type==LDNS_RR_TYPE_ANY || ldns_rr_get_type(rr)==type) && (ldns_dname_compare(name, ldns_rr_owner(rr))==0)) {
       fprintf(stderr, "Delete RR\n");
       ldns_rr_list_set_rr(rrs,ldns_rr_list_rr(rrs,--count),i--);
       ldns_rr_list_set_rr_count(rrs,count);
       ldns_rr_free(rr);
       result = true;
     }
   }
   return result;
}

void add_rr_list(ldns_zone* zone, ldns_rr_list *push_list) {
   ldns_rr_list* rrs = ldns_zone_rrs(zone);
   ldns_rr_list_push_rr_list(rrs, push_list);
}

void add_rr(ldns_zone* zone, ldns_rr *rr) {
   ldns_rr_list* rrs = ldns_zone_rrs(zone);
   ldns_rr_list_push_rr(rrs, rr);
}

void handle_dns_pkt(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt, int sock) {
  if (ldns_pkt_get_opcode(query_pkt)==LDNS_PACKET_QUERY) {
    pthread_mutex_lock(&mutex);
    handle_dns_query(query_pkt, answer_pkt, sock);
    pthread_mutex_unlock(&mutex);
  } else if (ldns_pkt_get_opcode(query_pkt)==LDNS_PACKET_UPDATE) {
    pthread_mutex_lock(&update_mutex);
    ldns_pkt_rcode rcode = handle_dns_update(query_pkt, answer_pkt);
    ldns_pkt_set_rcode(answer_pkt, rcode);
    pthread_mutex_unlock(&update_mutex);
  }
}

void handle_dns_query(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt, int sock) {
    ldns_rr_list *answer_an;
    ldns_rr_list *answer_ns;
    ldns_rr_list *answer_ad;

    size_t qdcount = ldns_pkt_qdcount(query_pkt);
    if (qdcount!=1) {
      ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_FORMERR);
      return;
    }
    
    ldns_rr *query_rr = ldns_rr_list_rr(ldns_pkt_question(query_pkt), 0);
    ldns_pkt_push_rr(answer_pkt, LDNS_SECTION_QUESTION, ldns_rr_clone(query_rr));

    ldns_zone *zone = zone_find(ldns_rr_owner(query_rr), ldns_rr_get_class(query_rr));
    ldns_rr2canonical(query_rr);
   
    if (zone) {
        ldns_pkt_set_aa(answer_pkt, 1);
        if (ldns_rr_get_type(query_rr)==LDNS_RR_TYPE_AXFR) {
            if (sock) {
                handle_axfr_request(zone, answer_pkt, sock);
                ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_ALREADY_HANDLED);
             } else {
                ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_SERVFAIL);
             }
             return;
        }

        answer_an = get_rrset(zone, ldns_rr_owner(query_rr), ldns_rr_get_type(query_rr), ldns_rr_get_class(query_rr), RRSET_CLONE|RRSET_FOLLOW_CNAME);
        answer_ns = ldns_rr_list_new();
        answer_ad = ldns_rr_list_new();

        ldns_rr_list_push_rr(answer_ns, ldns_rr_clone(ldns_zone_soa(zone)));

        ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_ANSWER, answer_an);
        ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_AUTHORITY, answer_ns);
        ldns_pkt_push_rr_list(answer_pkt, LDNS_SECTION_ADDITIONAL, answer_ad);
                
        ldns_rr_list_free(answer_an);
        ldns_rr_list_free(answer_ns);
        ldns_rr_list_free(answer_ad);

        ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_NOERROR);
    } else {
       ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_NXDOMAIN);
    }
} 

ldns_zone* ldns_zone_clone(ldns_zone *zone) {
  if (!zone) return NULL;
  ldns_rr *soa = ldns_rr_clone(ldns_zone_soa(zone));
  ldns_rr_list* rrs = ldns_rr_list_clone(ldns_zone_rrs(zone));
  zone=ldns_zone_new();
  ldns_zone_set_soa(zone, soa);
  ldns_zone_set_rrs(zone, rrs);
  return zone;
}

ldns_pkt_rcode handle_dns_update(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt) {
    if (ldns_update_zocount(query_pkt) != 1) {
      return LDNS_RCODE_FORMERR;
    }
      
    ldns_rr *zone_rr = ldns_rr_list_rr(ldns_pkt_question(query_pkt), 0);
    ldns_pkt_push_rr(answer_pkt, LDNS_SECTION_QUESTION, ldns_rr_clone(zone_rr));

    if (ldns_rr_get_type(zone_rr)!=LDNS_RR_TYPE_SOA) {
      return LDNS_RCODE_FORMERR;
    }

    pthread_mutex_lock(&mutex);
    ldns_zone *zone =  zone_find(ldns_rr_owner(zone_rr), ldns_rr_get_class(zone_rr));
    ldns_rdf* zname = ldns_rr_owner(zone_rr);
    ldns_rr_class zclass = ldns_rr_get_class(zone_rr);

    if (!zone) {
      #ifdef CAN_CREATE_ZONE
      //A create zone update must begin with the SOA RR
      ldns_rr *rr = ldns_rr_list_rr(ldns_pkt_authority(query_pkt), 0);
      if (!rr || ldns_rr_get_type(rr) != LDNS_RR_TYPE_SOA || ldns_dname_compare(ldns_rr_owner(rr),zname)) {
        return LDNS_RCODE_NOTAUTH;
      }
      #else
        return LDNS_RCODE_NOTAUTH;
      #endif
    }


    // if (zone_type(zname, zclass) == SLAVE) return forward()

    //Process Prerequisite Section
    fprintf(stderr, "Preq\n");
    for (size_t i=0; i<ldns_update_prcount(query_pkt); i++) {
      ldns_rr *rr = ldns_rr_list_rr(ldns_pkt_answer(query_pkt), i);

      if (ldns_rr_ttl(rr)) return LDNS_RCODE_FORMERR;
      if (zone_find(ldns_rr_owner(rr), zclass)!=zone) return LDNS_RCODE_NOTZONE;

      if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_ANY) {
         if (ldns_rr_rd_count(rr)) return LDNS_RCODE_FORMERR;
         if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_ANY) {
             if (!test_rrset(zone, rr, LDNS_RR_TYPE_ANY, LDNS_RR_CLASS_ANY)) {
               return LDNS_RCODE_NXDOMAIN;
             }
         } else {
            if (!test_rrset(zone, rr, ldns_rr_get_type(rr), LDNS_RR_CLASS_ANY)) {
               return LDNS_RCODE_NXRRSET;
            }
         }
      } else if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_NONE) {
         if (ldns_rr_rd_count(rr)) return LDNS_RCODE_FORMERR;
         if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_ANY) {
             if (test_rrset(zone, rr, LDNS_RR_TYPE_ANY, LDNS_RR_CLASS_ANY)) {
               return LDNS_RCODE_YXDOMAIN;
             }
         } else {
            if (test_rrset(zone, rr, ldns_rr_get_type(rr), LDNS_RR_CLASS_ANY)) {
               return LDNS_RCODE_YXRRSET;
            }
         }
       } else if (ldns_rr_get_class(rr) == zclass) {
          // build an RRset for each unique <NAME,TYPE> 
          // LDNS_RR_CLASS_ANYtemp<rr.name, rr.type> += rr
       } else {
           return LDNS_RCODE_FORMERR;
       }

       // for rrset in temp
       //if (zone_rrset<rrset.name, rrset.type> != rrset)
       //        return LDNS_RCODE_NXRRSET;
    }


    //prescan
    fprintf(stderr, "Prescan\n");
    for (size_t i=0; i<ldns_update_upcount(query_pkt); i++) {
      ldns_rr *rr = ldns_rr_list_rr(ldns_pkt_authority(query_pkt), i);

      if (zone_find(ldns_rr_owner(rr), zclass)!=zone) return LDNS_RCODE_NOTZONE;

      switch(ldns_rr_get_type(rr)) {
          case LDNS_RR_TYPE_A:
          case LDNS_RR_TYPE_AAAA:
          case LDNS_RR_TYPE_CNAME:
          case LDNS_RR_TYPE_TXT:
          case LDNS_RR_TYPE_SRV:
          case LDNS_RR_TYPE_HINFO:
          case LDNS_RR_TYPE_SOA:
              #ifdef CAN_DELETE_ZONE 
              if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_ANY && ldns_dname_compare(ldns_rr_owner(rr),zname)==0 && ldns_update_upcount(query_pkt)>1) { 
                //Delete zone updates must have upcount=1
                return LDNS_RCODE_FORMERR;
              }
              #endif
              break;
          default: return LDNS_RCODE_FORMERR;
      }

      if (ldns_rr_get_class(rr) == zclass) continue;
      if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_ANY && !ldns_rr_ttl(rr) && !ldns_rr_rd_count(rr)) continue;
      if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_NONE && !ldns_rr_ttl(rr)) continue;
      return LDNS_RCODE_FORMERR;
    }

    // update
    ldns_zone *original_zone =  zone;
    zone = ldns_zone_clone(original_zone);
    pthread_mutex_unlock(&mutex);

    bool increment_serial = false;
    for (uint16_t i=0; i<ldns_update_upcount(query_pkt); i++) {
      ldns_rr *rr = ldns_rr_list_rr(ldns_pkt_authority(query_pkt), i);

      if (ldns_rr_get_class(rr) == zclass) {
          if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_CNAME) {
              if (test_rrset(zone, rr, LDNS_RR_TYPE_ANY, zclass) && !test_rrset(zone, rr, LDNS_RR_TYPE_CNAME, zclass)) {
                continue;
              }
          } else if (test_rrset(zone, rr, LDNS_RR_TYPE_CNAME, zclass)) {
                continue;
          }
          
          if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA) {
               if (ldns_dname_compare(ldns_rr_owner(rr),zname)) {
                   fprintf(stderr, "Ignore SOA for name that is not a zone apex\n");
                   continue;
               } else if (zone==NULL) {
                 #ifdef CAN_CREATE_ZONE
                   if (ldns_dname_compare(ldns_rr_owner(rr),zname)==0) {
                   fprintf(stderr, "Create zone\n");
                   zone = ldns_zone_new();
                   ldns_zone_set_soa(zone, ldns_rr_clone(rr));
                 }
                 #endif
               } else {
                 ldns_rr *soa = ldns_zone_soa(zone);
                 if (!ldns_rr_rdf(soa, 2) || !ldns_rr_rdf(soa, 2)) continue;
                 uint32_t i1 = ldns_rdf2native_int32(ldns_rr_rdf(soa, 2));
                 uint32_t i2 = ldns_rdf2native_int32(ldns_rr_rdf(rr, 2));
                 if ((i1 < i2 && i2 - i1 < 0x7FFFFFFF) || (i1 > i2 && i1 - i2 > 0x7FFFFFFF)) {
                   fprintf(stderr, "Update SOA\n");
                   ldns_zone_set_soa(zone, rr=ldns_rr_clone(rr));
                   ldns_rr_free(soa);
                   increment_serial = false;
                 } else {
                   fprintf(stderr, "Ignore SOA with old serial\n");
                 }
               }
               continue;
          }

          bool replaced=false;
          ldns_rr_list *rrset = get_rrset(zone, ldns_rr_owner(rr), ldns_rr_get_type(rr), zclass, 0);
          for (size_t j=ldns_rr_list_rr_count(rrset); j-->0;) {
              if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_CNAME || 
                  ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA ||
                  ldns_rr_compare(rr, ldns_rr_list_rr(rrset,j))==0) {
//                zrr = rr
//                next [rr]
                  rr = NULL;
              }
          }
          ldns_rr_list_free(rrset);
          if (rr) add_rr(zone,ldns_rr_clone(rr));
          increment_serial = true;
      } else if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_ANY) {
          if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_ANY) {
              if (ldns_dname_compare(ldns_rr_owner(rr),zname)==0) {
                  ldns_rr_list *rrs = get_rrset(zone, zname, LDNS_RR_TYPE_NS, zclass, RRSET_CLONE);
                  increment_serial |= del_rr(zone, zname, LDNS_RR_TYPE_ANY);
                  add_rr_list(zone, rrs);
              } else {
                  increment_serial |= del_rr(zone, zname, LDNS_RR_TYPE_ANY);
              }
          } else if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA && ldns_dname_compare(ldns_rr_owner(rr),zname)==0) { 
              #ifdef CAN_DELETE_ZONE 
              fprintf(stderr, "Delete Zone\n");
              ldns_zone_deep_free(zone);
              zone_del(original_zone);
              return LDNS_RCODE_NOERROR;
              #endif
          } else if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_NS && ldns_dname_compare(ldns_rr_owner(rr),zname)==0) { 
              continue;
          } else {
              increment_serial |= del_rr(zone, ldns_rr_owner(rr), ldns_rr_get_type(rr));
          }

      } else if (ldns_rr_get_class(rr) == LDNS_RR_CLASS_NONE) {
            if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA) continue;
            //if (rr.type == NS && zone_rrset<rr.name, NS> == rr) continue;
            increment_serial |= del_rr_data(zone, rr);
      }
    }

    if (increment_serial) {
      ldns_rr_soa_increment(ldns_zone_soa(zone));
      ldns_rr *soa = ldns_zone_soa(zone);
      if (!ldns_rdf2native_int32(ldns_rr_rdf(soa, 2))) {
          ldns_rr_soa_increment(ldns_zone_soa(zone));
      }
    }

    pthread_mutex_lock(&mutex);
    zone_del(original_zone);
    zone_add(zone);
    pthread_mutex_unlock(&mutex);

    return LDNS_RCODE_NOERROR;
}

bool send_axfr_message(ldns_pkt* pkt, ldns_rr *rr, int sock) {
    ldns_pkt_push_rr(pkt, LDNS_SECTION_ANSWER, rr);

    uint8_t *outbuf=NULL;
    size_t answer_size;
    ldns_status status = ldns_pkt2wire(&outbuf, pkt, &answer_size);
     
    ldns_rr_list_pop_rr(ldns_pkt_answer(pkt));
    ldns_pkt_set_ancount(pkt,0);

    if (status != LDNS_STATUS_OK) {
        printf("Error creating answer: %s\n", ldns_get_errorstr_by_id(status));
        LDNS_FREE(outbuf);
        return false;
    } else {
        ldns_pkt_print(stdout, pkt);
    }

    if (outbuf) {
        uint16_t size = htons(answer_size);
        struct iovec iov[2] = {{&size,2}, {outbuf, answer_size}};
        writev(sock, iov, 2);
        LDNS_FREE(outbuf);
    }

    return true;
}

void handle_axfr_request(ldns_zone* zone, ldns_pkt* pkt, int sock) {
    zone = ldns_zone_clone(zone);
    pthread_mutex_unlock(&mutex);

    do {
      ldns_pkt_set_rcode(pkt, LDNS_RCODE_NOERROR);
      if (!send_axfr_message(pkt,ldns_zone_soa(zone),sock)) break;

      ldns_rr *rr;
      while (rr = ldns_rr_list_pop_rr(ldns_pkt_additional(pkt))) {
        ldns_rr_free(rr);
      }
      ldns_pkt_set_arcount(pkt,0);

      for (uint16_t i = 0; i < ldns_zone_rr_count(zone); i++) {
        rr = ldns_rr_list_rr(ldns_zone_rrs(zone), i);
        if (!send_axfr_message(pkt,rr,sock)) break;
      }
      if (!send_axfr_message(pkt,ldns_zone_soa(zone),sock)) break;
    } while(0);
    pthread_mutex_lock(&mutex);
}

void main() {
    zones = LDNS_CALLOC(ldns_zone*,1);
    struct in_addr dns_address = {0};
    int dns_port = 53;
    start_dns_server(dns_address, dns_port);
}

