#include "config.h"
#include <termios.h>
#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_NCURSESW_H
#include <ncursesw/curses.h>
#include <ncursesw/term.h>
#else
#include <curses.h>
#include <term.h>
#endif

#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else /* HAVE_FULL_QUEUE_H */
#include "compat/queue.h"
#endif /* HAVE_FULL_QUEUE_H */

#ifndef HAVE_STRLCPY
#include "compat/strlcpy.h"
#endif /* !HAVE_STRLCPY */

#ifndef HAVE_STRLCAT
#include "compat/strlcat.h"
#endif /* !HAVE_STRLCAT */

#include "choice.h"
#include "choices.h"
#include "ui.h"

#define KEY_CTRL_A 1
#define KEY_CTRL_B 2
#define KEY_CTRL_D 4
#define KEY_CTRL_E 5
#define KEY_CTRL_F 6
#define KEY_CTRL_N 14
#define KEY_CTRL_P 16
#define KEY_CTRL_K 11
#define KEY_CTRL_U 21
#define KEY_CTRL_W 23
#define KEY_DEL 127
#define KEY_REAL_ENTER 10 /* curses.h defines KEY_ENTER to be Ctrl-Enter */
#define KEY_ESCAPE 27
#define KEY_BRACKET 91
#define KEY_RAW_O 79
#define KEY_RAW_DOWN 66
#define KEY_RAW_UP 65
#define KEY_RAW_LEFT 68
#define KEY_RAW_RIGHT 67

#define EX_SIG 128
#define EX_SIGINT (EX_SIG + SIGINT)

static void start_ui();
static void stop_ui();
static void int_handler();
static void put_line(int, char *, int, int);
static int put_choices(struct choices *, int);
static struct choice *selected(struct choices *, int);
static void filter_choices(struct choices *, char *, int *);
static void del_chars_between(char *, size_t, size_t, size_t);
static int tty_putc(int);
static void putstrat(int, int, char *);
static void move_cursor_to(int, int);
static void tty_putp(const char *);

FILE *tty_out;
FILE *tty_in;
struct termios oldattr;
int using_alternate_screen;

struct choice *
get_selected(struct choices *cs, char *initial_query, int use_alternate_screen)
{
	char *query;
	int ch, sel, vis_choices, word_pos;
	size_t cursor_pos, query_size, query_len, initial_query_len;

	using_alternate_screen = use_alternate_screen;
	initial_query_len = strlen(initial_query);
	cursor_pos = initial_query_len;
	query_len = initial_query_len;
	query_size = 64;

	if (query_size < initial_query_len + 1) {
		query_size = initial_query_len + 1;
	}

	query = calloc(query_size, sizeof(char));
	if (query == NULL) {
		err(1, "calloc");
	}

	strlcpy(query, initial_query, query_size);

	filter_choices(cs, query, &sel);
	start_ui();

	put_line(0, query, query_len, 0);
	vis_choices = put_choices(cs, sel);
	move_cursor_to(0, cursor_pos);
	tty_putp(cursor_normal);

	while((ch = getc(tty_in)) != ERR) {
		switch(ch) {
		case KEY_REAL_ENTER:
			if (vis_choices > 0) {
				stop_ui();
				free(query);
				return selected(cs, sel);
			}
		case KEY_CTRL_N:
			if (sel < vis_choices - 1) {
				++sel;
			}

			break;
		case KEY_CTRL_P:
			if (sel > 0) {
				--sel;
			}

			break;
		case KEY_CTRL_B:
			if (cursor_pos > 0) {
				--cursor_pos;
			}

			break;
		case KEY_CTRL_F:
			if (cursor_pos < query_len) {
				++cursor_pos;
			}

			break;
		case KEY_BACKSPACE:
		case KEY_DEL:
			if (cursor_pos > 0) {
				del_chars_between(
				    query,
				    query_len,
				    cursor_pos - 1,
				    cursor_pos);
				--cursor_pos;
				--query_len;
				filter_choices(cs, query, &sel);
			}

			break;
		case KEY_CTRL_D:
			if (cursor_pos < query_len) {
				del_chars_between(
				    query,
				    query_len,
				    cursor_pos,
				    cursor_pos + 1);
				--query_len;
				filter_choices(cs, query, &sel);
			}

			break;
		case KEY_CTRL_U:
			del_chars_between(
			    query,
			    query_len,
			    0,
			    cursor_pos);
			query_len -= cursor_pos;
			cursor_pos = 0;
			filter_choices(cs, query, &sel);
			break;
		case KEY_CTRL_K:
			del_chars_between(
			    query,
			    query_len,
			    cursor_pos + 1,
			    query_len);
			query_len = cursor_pos;
			filter_choices(cs, query, &sel);
			break;
		case KEY_CTRL_W:
			if (cursor_pos > 0) {
				for (word_pos = cursor_pos - 1;
				    word_pos > 0;
				    --word_pos) {
					if (query[word_pos] != ' ' &&
					    query[word_pos - 1] == ' ') {
						break;
					}
				}

				del_chars_between(
				    query,
				    query_len,
				    word_pos,
				    cursor_pos);
				query_len -= cursor_pos - word_pos;
				cursor_pos = word_pos;
				filter_choices(cs, query, &sel);
			}
			break;
		case KEY_CTRL_A:
			cursor_pos = 0;
			break;
		case KEY_CTRL_E:
			cursor_pos = query_len;
			break;
		case KEY_ESCAPE:
			if((ch = getc(tty_in)) != ERR) {
				if (ch == KEY_BRACKET || ch == KEY_RAW_O) {
					if((ch = getc(tty_in)) != ERR) {
						switch (ch) {
						case KEY_RAW_DOWN:
							if (sel < vis_choices -
							    1) {
								++sel;
							}

							break;
						case KEY_RAW_UP:
							if (sel > 0) {
								--sel;
							}

							break;
						case KEY_RAW_LEFT:
							if (cursor_pos > 0) {
								--cursor_pos;
							}

							break;
						case KEY_RAW_RIGHT:
							if (cursor_pos <
							    query_len) {
								++cursor_pos;
							}

							break;
						}
					} else {
						err(1, "getc");
					}
				}
			} else {
				err(1, "getc");
			}

			break;
		default:
			if (ch > 31 && ch < 127) { /* Printable chars */
				if (cursor_pos < query_len) {
					memmove(
					    query + cursor_pos + 1,
					    query + cursor_pos,
					    query_len - cursor_pos);
				}

				query[cursor_pos++] = ch;
				query[++query_len] = '\0';
				filter_choices(cs, query, &sel);
			}

			break;
		}

		tty_putp(cursor_invisible);

		if (query_len == query_size - 1) {
			query_size += query_size;

			query = realloc(query, query_size * sizeof(char));
			if (query == NULL) {
				err(1, "realloc");
			}
		}

		put_line(0, query, query_len, 0);
		vis_choices = put_choices(cs, sel);
		move_cursor_to(0, cursor_pos);
		tty_putp(cursor_normal);
	}

	err(1, "getc");
}

static void
start_ui()
{
	struct termios newattr;

	if ((tty_in = fopen("/dev/tty", "r")) == NULL) {
		err(1, "fopen");
	}

	tcgetattr(fileno(tty_in), &oldattr);
	newattr = oldattr;
	newattr.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(fileno(tty_in), TCSANOW, &newattr);

	if ((tty_out = fopen("/dev/tty", "w")) == NULL) {
		err(1, "fopen");
	}

	setupterm((char *)0, fileno(tty_out), (int *)0);

	if (using_alternate_screen) {
		tty_putp(enter_ca_mode);
	}

	tty_putp(clear_screen);

	signal(SIGINT, int_handler);
}

static void
stop_ui()
{
	tcsetattr(fileno(tty_in), TCSANOW, &oldattr);
	fclose(tty_in);

	tty_putp(clear_screen);

	if (using_alternate_screen) {
		tty_putp(exit_ca_mode);
	}

	fclose(tty_out);
}

static void
int_handler()
{
	stop_ui();
	exit(EX_SIGINT);
}

static void
put_line(int y, char *str, int len, int so)
{
	if (so) {
		tty_putp(enter_standout_mode);
	}

	if (len > 0) {
		putstrat(y, 0, str);
	}

	move_cursor_to(y, len);

	for (; len < COLS; ++len) {
		tty_putc(' ');
	}

	if (so) {
		tty_putp(exit_standout_mode);
	}
}

static int
put_choices(struct choices *cs, int sel)
{
	char *line;
	int i;
	int vis_choices = 0;
	size_t len;
	size_t llen = 64;
	struct choice *c;

	line = malloc(sizeof(char) * llen);
	if (line == NULL) {
		err(1, "malloc");
	}

	SLIST_FOREACH(c, cs, choices) {
		len = strlen(c->str) + strlen(c->desc) + 1;

		while (len > llen) {
			llen = llen * 2;

			line = realloc(line, llen);
			if (line == NULL) {
				err(1, "realloc");
			}
		}

		strlcpy(line, c->str, llen);
		strlcat(line, " ", llen);
		strlcat(line, c->desc, llen);

		if (vis_choices == LINES - 1 || c->score == 0) {
			break;
		}

		put_line(vis_choices + 1, line, len, vis_choices == sel);

		++vis_choices;
	}

	free(line);

	for (i = vis_choices + 1; i < LINES; ++i) {
		put_line(i, "", 0, 0);
	}

	return vis_choices;
}

static struct choice *
selected(struct choices *cs, int sel)
{
	struct choice *c;
	int i = 0;

	SLIST_FOREACH(c, cs, choices) {
		if (c->score == 0) {
			break;
		}

		if (i == sel) {
			return c;
		}

		++i;
	}

	return NULL;
}

static void
filter_choices(struct choices *cs, char *query, int *sel)
{
	choices_score(cs, query);
	choices_sort(cs);
	*sel = 0;
}

static void
del_chars_between(char *str, size_t len, size_t start, size_t end)
{
	memmove(str + start, str + end, len - end + 1);
}

static int
tty_putc(int c)
{
	return putc(c, tty_out);
}

static void
putstrat(int y, int x, char *str)
{
	int i;

	move_cursor_to(y, x);

	for (i = 0; str[i] != '\0'; i++) {
		tty_putc(str[i]);
	}
}

static void
move_cursor_to(int y, int x)
{
	tty_putp(tgoto(cursor_address, x, y));
}

static void
tty_putp(const char *str)
{
	tputs(str, 1, tty_putc);
}
