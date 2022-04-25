/*	$OpenBSD: ex_init.c,v 1.19 2021/10/24 21:24:17 deraadt Exp $	*/

/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "tag.h"
#include "pathnames.h"

enum rc { NOEXIST, NOPERM, RCOK };
static enum rc	exrc_isok(SCR *, struct stat *, int *, char *);

static int ex_run_file(SCR *, int, char *);

/*
 * ex_screen_copy --
 *	Copy ex screen.
 *
 * PUBLIC: int ex_screen_copy(SCR *, SCR *);
 */
int
ex_screen_copy(SCR *orig, SCR *sp)
{
	EX_PRIVATE *oexp, *nexp;

	/* Create the private ex structure. */
	CALLOC_RET(orig, nexp, 1, sizeof(EX_PRIVATE));
	sp->ex_private = nexp;

	/* Initialize queues. */
	TAILQ_INIT(&nexp->tq);
	TAILQ_INIT(&nexp->tagfq);

	if (orig == NULL) {
	} else {
		oexp = EXP(orig);

		if (oexp->lastbcomm != NULL &&
		    (nexp->lastbcomm = strdup(oexp->lastbcomm)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return(1);
		}
		if (ex_tag_copy(orig, sp))
			return (1);
	}
	return (0);
}

/*
 * ex_screen_end --
 *	End a vi screen.
 *
 * PUBLIC: int ex_screen_end(SCR *);
 */
int
ex_screen_end(SCR *sp)
{
	EX_PRIVATE *exp;
	int rval;

	if ((exp = EXP(sp)) == NULL)
		return (0);

	rval = 0;

	/* Close down script connections. */
	if (F_ISSET(sp, SC_SCRIPT) && sscr_end(sp))
		rval = 1;

	if (argv_free(sp))
		rval = 1;

	free(exp->ibp);
	free(exp->lastbcomm);

	if (ex_tag_free(sp))
		rval = 1;

	/* Free private memory. */
	free(exp);
	sp->ex_private = NULL;

	return (rval);
}

/*
 * ex_optchange --
 *	Handle change of options for ex.
 *
 * PUBLIC: int ex_optchange(SCR *, int, char *, u_long *);
 */
int
ex_optchange(SCR *sp, int offset, char *str, u_long *valp)
{
	switch (offset) {
	case O_TAGS:
		return (ex_tagf_alloc(sp, str));
	}
	return (0);
}

/*
 * ex_exrc --
 *	Read the EXINIT environment variable and the startup exrc files,
 *	and execute their commands.
 *
 * !!!
 * this has been much simplified -- read ~/.exrc, read EXINIT
 *
 * PUBLIC: int ex_exrc(SCR *);
 */
int
ex_exrc(SCR *sp)
{
	struct stat hsb, lsb;
	char *p, path[PATH_MAX];
	int fd;

	if ((p = getenv("HOME")) != NULL && *p) {
		(void)snprintf(path, sizeof(path), "%s/%s", p, _PATH_EXRC);
		switch (exrc_isok(sp, &hsb, &fd, path)) {
			case NOEXIST:
			case NOPERM:
				break;
			case RCOK:
				if (ex_run_file(sp, fd, path))
					return (1);
		}
	}

	/* Run the commands. */
	if (EXCMD_RUNNING(sp->gp))
		(void)ex_cmd(sp);
	if (F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE))
		return (0);

	if ((p = getenv("EXINIT")) != NULL) {
		if (ex_run_str(sp, "EXINIT", p, strlen(p), 1, 0))
			return (1);
	}

	/* Run the commands. */
	if (EXCMD_RUNNING(sp->gp))
		(void)ex_cmd(sp);
	if (F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE))
		return (0);

	return (0);
}

/*
 * ex_run_file --
 *	Set up a file of ex commands to run.
 */
static int
ex_run_file(SCR *sp, int fd, char *name)
{
	ARGS *ap[2], a;
	EXCMD cmd;

	ex_cinit(&cmd, C_SOURCE, 0, OOBLNO, OOBLNO, 0, ap);
	ex_cadd(&cmd, &a, name, strlen(name));
	return (ex_sourcefd(sp, &cmd, fd));
}

/*
 * ex_run_str --
 *	Set up a string of ex commands to run.
 *
 * PUBLIC: int ex_run_str(SCR *, char *, char *, size_t, int, int);
 */
int
ex_run_str(SCR *sp, char *name, char *str, size_t len, int ex_flags,
    int nocopy)
{
	GS *gp;
	EXCMD *ecp;

	gp = sp->gp;
	if (EXCMD_RUNNING(gp)) {
		CALLOC_RET(sp, ecp, 1, sizeof(EXCMD));
		LIST_INSERT_HEAD(&gp->ecq, ecp, q);
	} else
		ecp = &gp->excmd;

	F_INIT(ecp,
	    ex_flags ? E_BLIGNORE | E_NOAUTO | E_NOPRDEF | E_VLITONLY : 0);

	if (nocopy)
		ecp->cp = str;
	else
		if ((ecp->cp = v_strdup(sp, str, len)) == NULL)
			return (1);
	ecp->clen = len;

	if (name == NULL)
		ecp->if_name = NULL;
	else {
		if ((ecp->if_name = v_strdup(sp, name, strlen(name))) == NULL)
			return (1);
		ecp->if_lno = 1;
		F_SET(ecp, E_NAMEDISCARD);
	}

	return (0);
}

/*
 * exrc_isok --
 *	Open and check a .exrc file for source-ability.
 *
 * !!!
 * This has been very much simplified on the assumption that files
 * cannot be "given away" (SysV crazy?) and that the user knows
 * how to manage the permissions of their dotfiles.
 */
static enum rc
exrc_isok(SCR *sp, struct stat *sbp, int *fdp, char *path)
{
	if ((*fdp = open(path, O_RDONLY, 0)) < 0) {
		if (errno == ENOENT)
			return (NOEXIST);

		msgq_str(sp, M_SYSERR, path, "%s");
		return (NOPERM);
	}
	return (RCOK);
}
