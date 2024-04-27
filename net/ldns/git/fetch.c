#include "common.h"
#include <stdio.h>

static int progress_cb(const char *str, int len, void *data)
{
	(void)data;
	fprintf(stderr, "remote: %.*s", len, str);
	fflush(stderr); /* We don't have the \n to force the flush */
	return 0;
}

/**
 * This function gets called for each remote-tracking branch that gets
 * updated. The message we output depends on whether it's a new one or
 * an update.
 */
static int update_cb(const char *refname, const git_oid *a, const git_oid *b, void *data)
{
	char a_str[GIT_OID_SHA1_HEXSIZE+1], b_str[GIT_OID_SHA1_HEXSIZE+1];
	(void)data;

	git_oid_fmt(b_str, b);
	b_str[GIT_OID_SHA1_HEXSIZE] = '\0';

	if (git_oid_is_zero(a)) {
		fprintf(stderr, "[new]     %.20s %s\n", b_str, refname);
	} else {
		git_oid_fmt(a_str, a);
		a_str[GIT_OID_SHA1_HEXSIZE] = '\0';
		fprintf(stderr, "[updated] %.10s..%.10s %s\n", a_str, b_str, refname);
	}

	return 0;
}

/**
 * This gets called during the download and indexing. Here we show
 * processed and total objects in the pack and the amount of received
 * data. Most frontends will probably want to show a percentage and
 * the download rate.
 */
static int transfer_progress_cb(const git_indexer_progress *stats, void *payload)
{
	(void)payload;

	if (stats->received_objects == stats->total_objects) {
		fprintf(stderr, "Resolving deltas %u/%u\r",
		       stats->indexed_deltas, stats->total_deltas);
	} else if (stats->total_objects > 0) {
		fprintf(stderr, "Received %u/%u objects (%u) in %" PRIuZ " bytes\r",
		       stats->received_objects, stats->total_objects,
		       stats->indexed_objects, stats->received_bytes);
	}
	return 0;
}

/** Entry point for this command */
int lg2_fetch(git_repository *repo)
{
	git_remote *remote = NULL;
	const git_indexer_progress *stats;
	git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;

	/* Figure out whether it's a named remote or a URL */
	//iif (git_remote_lookup(&remote, repo, name) < 0)
	//	if (git_remote_create_anonymous(&remote, repo, name) < 0)
        //	       goto on_error;

        if (!check_lg2(git_remote_lookup(&remote, repo, "origin" ), "Unable to lookup remote")) return -1;

	/* Set up the callbacks (only update_tips for now) */
	fetch_opts.callbacks.update_tips = &update_cb;
	fetch_opts.callbacks.sideband_progress = &progress_cb;
	fetch_opts.callbacks.transfer_progress = transfer_progress_cb;
	fetch_opts.callbacks.credentials = cred_acquire_cb;

	if (!check_lg2(git_remote_fetch(remote, NULL, &fetch_opts, "fetch"), "Fetch failed"))
		goto on_error;

	/**
	 * If there are local objects (we got a thin pack), then tell
	 * the user how many objects we saved from having to cross the
	 * network.
	 */
	stats = git_remote_stats(remote);
	if (stats->local_objects > 0) {
		fprintf(stderr, "\rReceived %u/%u objects in %" PRIuZ " bytes (used %u local objects)\n",
		       stats->indexed_objects, stats->total_objects, stats->received_bytes, stats->local_objects);
	} else{
		fprintf(stderr, "\rReceived %u/%u objects in %" PRIuZ " bytes\n",
			stats->indexed_objects, stats->total_objects, stats->received_bytes);
	}

	git_remote_free(remote);

	return 0;

 on_error:
	git_remote_free(remote);
	return -1;
}
