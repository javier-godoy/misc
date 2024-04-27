#include "common.h"
#include <stdio.h>

typedef struct progress_data {
	git_indexer_progress fetch_progress;
	size_t completed_steps;
	size_t total_steps;
	const char *path;
} progress_data;

static void print_progress(const progress_data *pd)
{
	int network_percent = pd->fetch_progress.total_objects > 0 ?
		(100*pd->fetch_progress.received_objects) / pd->fetch_progress.total_objects :
		0;
	int index_percent = pd->fetch_progress.total_objects > 0 ?
		(100*pd->fetch_progress.indexed_objects) / pd->fetch_progress.total_objects :
		0;

	int checkout_percent = pd->total_steps > 0
		? (int)((100 * pd->completed_steps) / pd->total_steps)
		: 0;
	size_t kbytes = pd->fetch_progress.received_bytes / 1024;

	if (pd->fetch_progress.total_objects &&
		pd->fetch_progress.received_objects == pd->fetch_progress.total_objects) {
		fprintf(stderr, "Resolving deltas %u/%u\r",
		       pd->fetch_progress.indexed_deltas,
		       pd->fetch_progress.total_deltas);
	}
}

static int sideband_progress(const char *str, int len, void *payload)
{
	(void)payload; /* unused */

	fprintf(stderr, "remote: %.*s", len, str);
	fflush(stdout);
	return 0;
}

static int fetch_progress(const git_indexer_progress *stats, void *payload)
{
	progress_data *pd = (progress_data*)payload;
	pd->fetch_progress = *stats;
	print_progress(pd);
	return 0;
}
static void checkout_progress(const char *path, size_t cur, size_t tot, void *payload)
{
	progress_data *pd = (progress_data*)payload;
	pd->completed_steps = cur;
	pd->total_steps = tot;
	pd->path = path;
	print_progress(pd);
}


int lg2_clone(git_repository **repo, const char* url, const char* path)
{
	progress_data pd = {{0}};
	git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;

	/* Set up options */
	checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
	checkout_opts.progress_cb = checkout_progress;
	checkout_opts.progress_payload = &pd;
	clone_opts.checkout_opts = checkout_opts;
	clone_opts.fetch_opts.callbacks.sideband_progress = sideband_progress;
	clone_opts.fetch_opts.callbacks.transfer_progress = &fetch_progress;
	clone_opts.fetch_opts.callbacks.credentials = cred_acquire_cb;
	clone_opts.fetch_opts.callbacks.payload = &pd;

	/* Do the clone */
//	git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
	int error = git_clone(repo, url, path, &clone_opts);
	fprintf(stderr, "\n");
	if (error != 0) {
		const git_error *err = git_error_last();
		if (err) fprintf(stderr, "ERROR %d: %s\n", err->klass, err->message);
		else fprintf(stderr, "ERROR %d: no detailed info\n", error);
	}
	return error;
}
