/*
 * Utilities library for libgit2 examples
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

#ifndef INCLUDE_examples_common_h__
#define INCLUDE_examples_common_h__

#include "git2.h"
#include <stdbool.h>
#define PRIuZ "zu"

bool check_lg2(int error, const char *message);
bool check_lg2_extra(int error, const char *message, const char *extra);

int lg2_clone(git_repository **repo, const char* url, const char* path);
int lg2_fetch(git_repository *repo);
int lg2_commit(git_repository *repo, const char* comment, git_oid* lease);
int lg2_push(git_repository *repo, git_oid* lease);

int cred_acquire_cb(git_credential **out,
		const char *url,
		const char *username_from_url,
		unsigned int allowed_types,
		void *payload);

#endif
