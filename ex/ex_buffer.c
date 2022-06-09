#include "config.h"

#include <sys/queue.h>

#include <bitstring.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"

/*
 * ex_buffer -- :buffer string 
 *	Alter the contents of a given buffer.
 *
 * PUBLIC: int ex_buffer(SCR *, EXCMD *);
 */
int
ex_buffer(SCR *sp, EXCMD *cmdp)
{
	CB *cbp;
	CHAR_T name;
	TEXT *tp;
	int append;
	size_t len;

	switch (cmdp->argc) {
	case 2:
		break;
	default:
		abort();
	}

	name = *cmdp->argv[0]->bp;
	if ((append = isupper(name)) == 1) { name = tolower(name); }
	CBNAME(sp, cbp, name);

	if (cbp == NULL) {
		CALLOC_RET(sp, cbp, 1, sizeof(CB));
		cbp->name = name;
		TAILQ_INIT(&cbp->textq);
		LIST_INSERT_HEAD(&sp->gp->cutq, cbp, q);
	} else if (!append) {
		text_lfree(&cbp->textq);
		cbp->len   = 0;
		cbp->flags = 0;
	}

	len = cmdp->argv[1]->len;

	if ((tp = text_init(sp, NULL, 0, len)) == NULL) return (1);

	memcpy(tp->lb, cmdp->argv[1]->bp, len);
	tp->len = len;

	TAILQ_INSERT_TAIL(&cbp->textq, tp, q);
	cbp->len += tp->len;

	return (0);
}
