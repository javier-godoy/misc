/*
 * libgit2 "push" example - shows how to push to remote
 *
 * Written by the libgit2 contributors
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "common.h"
#include <stdio.h>
int cb_push_negotiation_force_with_lease(const git_push_update **updates, size_t len, void *payload) {
    for (size_t i=len; i-->0;) {
      char src[9], dst[9], oid[9];
      git_oid_tostr(src, sizeof src, &updates[i]->src);
      git_oid_tostr(dst, sizeof dst, &updates[i]->dst);
      if (payload && !git_oid_equal((git_oid*)payload, &updates[i]->src)) {
        git_oid_tostr(oid, sizeof oid, (git_oid*)payload);
        fprintf(stderr, "Push conflict: remote is at %s, expected %s\n", src, oid);
        return GIT_ENONFASTFORWARD;
      }
      fprintf(stderr, "Push %s %s -> %s\n", updates[i]->src_refname,src,dst);
    } 
    return GIT_OK; 
}

int lg2_push(git_repository *repo, git_oid* oid) {
        int error = GIT_ERROR;

	git_push_options options;
	git_remote_callbacks callbacks;
	git_remote* remote = NULL;
	char *refspec = "+refs/heads/master";
	const git_strarray refspecs = { &refspec, 1 };

	if (!check_lg2(git_remote_lookup(&remote, repo, "origin" ), "Unable to lookup remote")) goto _0;
	
	if (!check_lg2(git_remote_init_callbacks(&callbacks, GIT_REMOTE_CALLBACKS_VERSION), "Error initializing remote callbacks")) goto _1;
	callbacks.credentials = cred_acquire_cb;
        callbacks.push_negotiation = &cb_push_negotiation_force_with_lease;
        callbacks.payload=oid;

	if (!check_lg2(git_push_options_init(&options, GIT_PUSH_OPTIONS_VERSION ), "Error initializing push")) goto _1;
	options.callbacks = callbacks;
	error = git_remote_push(remote, &refspecs, &options);
	if (!check_lg2(error, "Error pushing")) goto _1;

        error = GIT_OK;
     _1: git_remote_free(remote);
     _0: return error;
}

