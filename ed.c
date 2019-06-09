/*
 * ed -- a simplified version of GNU ed
 *
 * This is a school assignment. It may be useful as a learning resource,
 * but it's not production grade software.
 *
 * David Čepelík <d@dcepelik.cz> (c) 2019
 *
 * Distributed under the terms of the MIT License.
 */

#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LINE_MAX_LEN 1024

struct line {
	char *text;
	struct line *next;
	struct line *prev;
	long int line_no;
};

static char *read_line(FILE *f)
{
	char buf[LINE_MAX_LEN];
	if (!fgets(buf, sizeof(buf), f)) {
		if (feof(f))
			return NULL;
		err(1, "fgets");
	}
	size_t l = strlen(buf);
	assert(l > 0);
	if (buf[l - 1] != '\n' && !feof(f))
		errx(1, "input line longer than %zu bytes", sizeof(buf));
	char *str = malloc(l + 1);
	if (!str)
		err(1, "malloc");
	strncpy(str, buf, l);
	str[l] = '\0';
	return str;
}

enum err {
	E_NONE,
	E_BAD_ADDR,
	E_BAD_CMD_SUFFIX,
	E_UNEXP_ADDR,
	E_CMD,
	E_INPUT,
};

struct buffer {
	struct line *first;
	long int nlines;
	long int cur_line;
	int print_errors;
};

static void buffer_init(struct buffer *b)
{
	b->first = NULL;
	b->nlines = 0;
	b->cur_line = 0;
	b->print_errors = 0;
}

static void buffer_init_load(struct buffer *b, FILE *f)
{
	size_t l = 0;
	struct line *first = NULL, *prev = NULL;
	char *text;
	b->nlines = 0;
	while ((text = read_line(f)) != NULL) {
		if (l > LINE_MAX_LEN)
			errx(1, "input line too long at %zu bytes", l);
		struct line *l = malloc(sizeof(*l));
		if (!first)
			first = l;
		l->text = text;
		l->prev = prev;
		if (prev)
			prev->next = l;
		prev = l;
		l->next = NULL;
		b->nlines++;
		l->line_no = b->nlines;
	}
	b->first = first;
	b->cur_line = b->nlines;
	b->print_errors = 0;
}

static void buffer_free(struct buffer *buf)
{
	struct line *line = buf->first, *prev;
	while (line) {
		free(line->text);
		prev = line;
		line = line->next;
		free(prev);
	}
}

static void xusage(int eval, char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "usage: ed file\n");
	va_end(va);
	exit(eval);
}

struct cmd {
	char cmd;
	long int a;
	long int b;
	int addr_given;
};

static long int parse_lineno(char *str, char **endp, struct buffer *buf)
{
	if (*str != '-' && !isdigit(*str) && *str != '.' && *str != '$') {
		*endp = str;
		return 0;
	}
	long int n = strtol(str, endp, 10);
	if (*endp != str) {
		if (n < 0)
			return buf->cur_line + n;
		return n;
	}
	if (*str == '.') {
		*endp = str + 1;
		return buf->cur_line;
	}
	if (*str == '$') {
		*endp = str + 1;
		return buf->nlines;
	}
	*endp = str;
	return buf->cur_line;
}

static int parse_command(struct buffer *buf, struct cmd *cmd)
{
	enum err e = E_NONE;
	cmd->b = 0;
	cmd->addr_given = 0;
	char *str = read_line(stdin), *ostr = str;
	if (!str) {
		cmd->cmd = 'q';
		cmd->addr_given = 0;
		return E_NONE;
	}
	if (*str == ' ') {
		e = E_BAD_ADDR;
		goto out_err;
	}
	char *endp;
	int have_comma = (index(str, ',') != NULL);
	cmd->a = parse_lineno(str, &endp, buf);
	if (endp != str) {
		str = endp;
		if (*str == ',') {
			str++;
			cmd->b = parse_lineno(str, &endp, buf);
			if (endp == str) {
				e = E_BAD_ADDR;
				goto out_err;
			}
			str = endp;
		} else if (have_comma) {
			e = E_BAD_ADDR;
			goto out_err;
		} else {
			cmd->b = cmd->a;
		}
		cmd->addr_given = 1;
	} else if (have_comma) {
		e = E_BAD_ADDR;
		goto out_err;
	}
	cmd->cmd = *str++;
	if (*str != '\0' && *str != '\n')
		e = E_BAD_CMD_SUFFIX;
out_err:
	free(ostr);
	return e;
}

static void buffer_print_range(struct buffer *buf, long int a, long int b,
			       int with_line_numbers)
{
	struct line *line;
	for (line = buf->first; line != NULL; line = line->next) {
		if (line->line_no > b)
			break;
		if (line->line_no < a)
			continue;
		if (with_line_numbers)
			printf("%li\t%s", line->line_no, line->text);
		else
			printf("%s", line->text);
	}
}

static const char *buf_err_str(enum err e)
{
	switch (e) {
	case E_NONE:
		return "No error";
	case E_BAD_ADDR:
		return "Invalid address";
	case E_BAD_CMD_SUFFIX:
		return "Invalid command suffix";
	case E_UNEXP_ADDR:
		return "Unexpected address";
	case E_CMD:
		return "Unknown command";
	case E_INPUT:
		return "Cannot open input file";
	default:
		assert(0);
	}
}

static int buffer_validate_addr(struct buffer *buf, struct cmd *cmd)
{
	return cmd->a <= cmd->b && cmd->a >= 1 && cmd->a <= buf->nlines &&
	       cmd->b >= 1 && cmd->b <= buf->nlines;
}

int main(int argc, const char *argv[])
{
	enum err err = E_NONE, err2, last_err = E_NONE;
	if (argc != 2)
		xusage(1, "");
	const char *fname = argv[1];
	if (fname[0] == '-')
		xusage(1, "ed: illegal option -- %s\n", fname + 1);

	struct buffer buf;
	FILE *f = fopen(fname, "r");
	if (!f) {
		fprintf(stderr, "%s: %s\n", fname, strerror(errno));
		buffer_init(&buf);
		last_err = E_INPUT;
	} else {
		buffer_init_load(&buf, f);
		fseek(f, 0, SEEK_END);
		printf("%lu\n", ftell(f));
		fclose(f);
	}

	struct cmd cmd;
	for (;;) {
		err2 = parse_command(&buf, &cmd);
		if (!cmd.addr_given) {
			if (cmd.cmd == '\n')
				cmd.a = cmd.b = buf.cur_line + 1;
			else
				cmd.a = cmd.b = buf.cur_line;
		}
		if (err2 != E_NONE && err2 != E_BAD_CMD_SUFFIX) {
			err = err2;
			goto skip_cmd;
		}
		if ((cmd.addr_given || cmd.cmd == '\n') &&
		    !buffer_validate_addr(&buf, &cmd)) {
			err = E_BAD_ADDR;
			goto skip_cmd;
		}
		switch (cmd.cmd) {
		case '\n':
		case 'n':
		case 'p':
		case 'H':
		case 'h':
		case 'q':
			break;
		default:
			err = E_CMD;
			goto skip_cmd;
		}
		if (err2 == E_BAD_CMD_SUFFIX) {
			err = err2;
			goto skip_cmd;
		}
		switch (cmd.cmd) {
		case '\n':
		case 'n':
		case 'p':
			buffer_print_range(&buf, cmd.a, cmd.b, cmd.cmd == 'n');
			buf.cur_line = cmd.b;
			err = E_NONE;
			break;
		case 'H':
			if (cmd.addr_given) {
				err = E_UNEXP_ADDR;
			} else {
				buf.print_errors = !buf.print_errors;
				if (buf.print_errors && last_err != E_NONE)
					puts(buf_err_str(last_err));
				continue;
			}
			break;
		case 'h':
			if (cmd.addr_given) {
				err = E_UNEXP_ADDR;
				break;
			}
			else if (last_err != E_NONE)
				puts(buf_err_str(last_err));
			continue;
		case 'q':
			if (cmd.addr_given)
				err = E_UNEXP_ADDR;
			else
				goto quit;
			break;
		default:
			err = E_CMD;
		}
		if (err == E_NONE && err2 != E_NONE)
			err = err2; // Just a hack to satisfy the assignment.
	skip_cmd:
		if (err != E_NONE) {
			puts("?");
			if (buf.print_errors)
				puts(buf_err_str(err));
			last_err = err;
		}
	}
quit:
	buffer_free(&buf);
	return last_err == E_NONE || last_err == E_INPUT ? 0 : 1;
}
