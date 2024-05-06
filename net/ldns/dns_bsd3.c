/*
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

#include "dns_server.h"

extern opts_struct opts;
extern int udp_sock;

pthread_rwlock_t lock;
pthread_mutex_t update_mutex;

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
    
    if (ldns_pkt_qr(query_pkt) && ldns_pkt_get_opcode(query_pkt)!=LDNS_PACKET_NOTIFY) {
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

void handle_dns_pkt(const ldns_pkt* query_pkt, ldns_pkt* answer_pkt, int sock) {
    if (ldns_pkt_get_opcode(query_pkt)==LDNS_PACKET_QUERY) {
        pthread_rwlock_rdlock(&lock);
        handle_dns_query(query_pkt, answer_pkt, sock);
        pthread_rwlock_unlock(&lock);
    } else if (ldns_pkt_get_opcode(query_pkt)==LDNS_PACKET_UPDATE) {
        pthread_mutex_lock(&update_mutex);
        ldns_pkt_rcode rcode = handle_dns_update(query_pkt, answer_pkt);
        ldns_pkt_set_rcode(answer_pkt, rcode);
        pthread_mutex_unlock(&update_mutex);
    } else if (ldns_pkt_get_opcode(query_pkt)==LDNS_PACKET_NOTIFY) {
        ldns_pkt_rcode rcode = handle_dns_notify(query_pkt, answer_pkt);
        ldns_pkt_set_rcode(answer_pkt, rcode);
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
    ldns_rr2canonical(query_rr);

    ldns_zone *zone = zone_find(ldns_rr_owner(query_rr), ldns_rr_get_class(query_rr));
   
    ldns_pkt_push_rr(answer_pkt, LDNS_SECTION_QUESTION, ldns_rr_clone(query_rr));

    if (zone) {
        ldns_pkt_set_aa(answer_pkt, 1);
        if (ldns_rr_get_type(query_rr)==LDNS_RR_TYPE_AXFR) {
            if (sock) {
                zone = ldns_zone_clone(zone);
                pthread_rwlock_unlock(&lock);
                handle_axfr_request(zone, answer_pkt, sock);
                ldns_pkt_set_rcode(answer_pkt, LDNS_RCODE_ALREADY_HANDLED);
                ldns_zone_deep_free(zone);
                pthread_rwlock_rdlock(&lock);
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

void send_notify(ldns_zone* zone) {
    ldns_rr *soa = ldns_zone_soa(zone);
    if (!soa) return;

    ldns_pkt* notify = ldns_pkt_new();
    ldns_rr *question = ldns_rr_new();

    ldns_rr_set_class(question, ldns_rr_get_class(soa));
    ldns_rr_set_owner(question, ldns_rdf_clone(ldns_rr_owner(soa)));
    ldns_rr_set_type(question, LDNS_RR_TYPE_SOA);
    ldns_rr_set_question(question, true);
    ldns_pkt_set_opcode(notify, LDNS_PACKET_NOTIFY);
    ldns_pkt_push_rr(notify, LDNS_SECTION_QUESTION, question);
    ldns_pkt_set_aa(notify, true);
    ldns_pkt_set_random_id(notify);

    uint8_t *wire = NULL;
    size_t wiresize = 0;

    ldns_status status = ldns_pkt2wire(&wire, notify, &wiresize);
    ldns_pkt_free(notify);
    if (status) {
        fprintf(stderr, "Error converting notify packet to hex: %s\n",
                ldns_get_errorstr_by_id(status));
        return;
    } 

    ldns_rr_list *rrlist_a  = ldns_rr_list_new();
    ldns_rr_list *rrlist_ns = get_rrset(zone, ldns_rr_owner(soa), LDNS_RR_TYPE_NS, ldns_rr_get_class(soa), 0);
    for (uint16_t i=ldns_rr_list_rr_count(rrlist_ns); i-->0;) {
        ldns_rr *rr = ldns_rr_list_rr(rrlist_ns,i);
        ldns_rdf *nsdname = ldns_rr_ns_nsdname(rr);
        ldns_rr_list *rrs = get_rrset(zone_find(nsdname, LDNS_RR_CLASS_IN), nsdname, LDNS_RR_TYPE_A, LDNS_RR_CLASS_IN, 0);
        if (ldns_rr_list_rr_count(rrs)) {
            ldns_rr* rr_a = ldns_rr_list_rr(rrs,0);
            ldns_rdf* address = ldns_rr_a_address(rr_a);
            if (ldns_rdf_size(address) == 4
             && ldns_rdf_get_type(address)==LDNS_RDF_TYPE_A
             && memcmp(ldns_rdf_data(address), &opts.address, 4)!=0) {
                ldns_rr_list_push_rr(rrlist_a, ldns_rr_clone(rr_a)); 
            }
        }
        ldns_rr_list_free(rrs);
    }

    ldns_rr_list_free(rrlist_ns);
    for (uint16_t i=ldns_rr_list_rr_count(rrlist_a); i-->0;) {
       ldns_rr *rr = ldns_rr_list_rr(rrlist_a,i);
       ldns_rdf* address = ldns_rr_a_address(rr);
       struct sockaddr_in addr;
       addr.sin_family = AF_INET;
       addr.sin_port = htons(53);
       memcpy(&addr.sin_addr.s_addr,ldns_rdf_data(address),4);
       sendto(udp_sock, (void*)wire, wiresize, 0, (struct sockaddr*)&addr, sizeof addr);
    }

    ldns_rr_list_deep_free(rrlist_a);
    LDNS_FREE(wire);
}

