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

#include "common.h"
#include <string.h>
#include <stdio.h>

int cred_acquire_cb(git_credential **out, 
		const char *url,
		const char *username,
		unsigned int allowed_types,
		void *payload) {		
	const char* privkey = "/root/.ssh/id_ed25519";
	const char* pubkey  = "/root/.ssh/id_ed25519.pub";	
	return git_credential_ssh_key_new(out, "git", pubkey, privkey, "git");
}

bool check_lg2(int error, const char *message) 
{
  return check_lg2_extra(error,message,NULL);
}

bool check_lg2_extra(int error, const char *message, const char *extra)
{
	const git_error *lg2err;
	const char *lg2msg = "", *lg2spacer = "";

	if (!error)
		return true;

	if ((lg2err = git_error_last()) != NULL && lg2err->message != NULL) {
		lg2msg = lg2err->message;
		lg2spacer = " - ";
	}

	if (extra)
		fprintf(stderr, "%s '%s' [%d]%s%s\n",
			message, extra, error, lg2spacer, lg2msg);
	else
		fprintf(stderr, "%s [%d]%s%s\n",
			message, error, lg2spacer, lg2msg);

	return false;
}

