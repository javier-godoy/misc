/*
 * libgit2 "commit" example - shows how to create a git commit
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

int lg2_commit(git_repository *repo, const char* comment, git_oid *lease)
{
	int error = GIT_ERROR;

	git_oid commit_oid,tree_oid;
	git_tree *tree;
	git_index *index;
	git_object *parent = NULL;
	git_reference *ref = NULL;
	git_signature *signature;

	if (!check_lg2(git_revparse_ext(&parent, &ref, repo, "HEAD"), "HEAD commit not found")||!parent) goto _0;
	if (!check_lg2(git_repository_index(&index, repo), "Could not open repository index")) goto _1;
	if (!check_lg2(git_index_write_tree(&tree_oid, index), "Could not write tree")) goto _2;
    	if (!check_lg2(git_index_write(index), "Could not write index") ||
 	    !check_lg2(git_tree_lookup(&tree, repo, &tree_oid), "Error looking up tree") ||
            !check_lg2(git_signature_now(&signature, "DNS", "dns@local"), "Error creating signature")) goto _3;

        if (git_commit_parentcount((git_commit*)parent)) {
           if (!check_lg2(git_commit_amend(
               &commit_oid,
               (const git_commit*) parent,
                "HEAD",
		signature,
		signature,
		NULL,
		comment,
		tree), "Error creating commit")) goto _4;
        } else {
	    if (!check_lg2(git_commit_create_v(
		&commit_oid,
		repo,
		"HEAD",
		signature,
		signature,
		NULL,
		comment,
		tree,
		1, parent), "Error creating commit")) goto _4;
        }


        error = GIT_OK;
        *lease = *git_object_id(parent);

    _4:
	git_signature_free(signature);

    _3:
	git_tree_free(tree);
    _2:
	git_index_free(index);

    _1:
	git_object_free(parent);
	git_reference_free(ref);
    _0:
	return error;
}
