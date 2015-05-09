#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else /* HAVE_FULL_QUEUE_H */
#include "compat/queue.h"
#endif /* HAVE_FULL_QUEUE_H */

#include "choice.h"
#include "choices.h"

static size_t min_match_length(char *, char *);
static float score_str(char *, char *);
static struct choice *merge(struct choice *, struct choice *);
static struct choice *sort(struct choice *c);

void
choices_score(struct choices *cs, char *query)
{
	struct choice *c;

	SLIST_FOREACH(c, cs, choices) {
		c->score = score_str(c->str, query);
	}
}

void
choices_sort(struct choices *cs)
{
	cs->slh_first = sort(cs->slh_first);
}

void
choices_free(struct choices *cs)
{
	struct choice *c;

	while (!SLIST_EMPTY(cs)) {
		c = SLIST_FIRST(cs);
		SLIST_REMOVE_HEAD(cs, choices);
		choice_free(c);
	}

	free(cs);
}

size_t
min_match_length(char *str, char *query)
{
	size_t mlen, mstart, qpos, mpos;
	int query_char, query_start;

	query_start = tolower((unsigned char)query[0]);

	for (mlen = 0, mstart = 0; str[mstart] != '\0'; ++mstart) {
		if (tolower((unsigned char)str[mstart]) == query_start) {
			for (qpos = 1, mpos = mstart + 1; query[qpos] != '\0';
			    ++qpos) {
				query_char =
				    tolower((unsigned char)query[qpos]);

				for (;; ++mpos) {
					if (str[mpos] == '\0') {
						return mlen;
					}

					if (tolower((unsigned char)str[mpos]) ==
					    query_char) {
						++mpos;
						break;
					}
				}
			}
			if (mlen == 0 || mlen > mpos - mstart + 1) {
				mlen = mpos - mstart + 1;
			}
		}
	}
	return mlen;
}

static float
score_str(char *str, char *query)
{
	size_t slen, qlen, mlen;

	slen = strlen(str);
	qlen = strlen(query);

	if (qlen == 0) {
		return 1;	
	}

	if (slen == 0) {
		return 0;
	}

	mlen = min_match_length(str, query);
	if (mlen == 0) {
		return 0;
	}

	return (float)qlen / (float)mlen / (float)slen;
}

static struct choice *
merge(struct choice *front, struct choice *back)
{
	struct choice head;
	struct choice *c;

	c = &head;

	while (front != NULL && back != NULL) {
		if (front->score > back->score ||
		    (front->score == back->score &&
		     strcmp(front->str, back->str) < 0)) {
			c->choices.sle_next = front;
			c = front;
			front = front->choices.sle_next;
		} else {
			c->choices.sle_next = back;
			c = back;
			back = back->choices.sle_next;
		}
	}

	if (front != NULL) {
		c->choices.sle_next = front;
	} else {
		c->choices.sle_next = back;
	}

	return head.choices.sle_next;
}

static struct choice *
sort(struct choice *c)
{
	struct choice *front, *back;

	if (c == NULL || c->choices.sle_next == NULL) {
		return c;
	}

	front = c;
	back = c->choices.sle_next;

	while (back != NULL && back->choices.sle_next != NULL) {
		c = c->choices.sle_next;
		back = back->choices.sle_next->choices.sle_next;
	}

	back = c->choices.sle_next;
	c->choices.sle_next = NULL;

	return merge(sort(front), sort(back));
}
