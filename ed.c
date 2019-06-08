#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 1024

struct line
{
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

enum err
{
	E_NONE,
	E_BAD_ADDR,
	E_BAD_CMD_SUFFIX,
	E_UNEXP_ADDR,
	E_CMD,
};

struct buffer
{
	struct line *first;
	long int nlines;
	long int cur_line;
	int print_errors;
};

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

static void xusage(int eval, char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "usage: ed file\n");
	va_end(va);
	exit(eval);
}

struct cmd
{
	char cmd;
	long int a;
	long int b;
	int addr_given;
};

static long int parse_lineno(char *str, char **endp, struct buffer *buf)
{
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

// TODO 1,10nn
static int parse_command(struct buffer *buf, struct cmd *cmd)
{
	cmd->b = 0;
	cmd->addr_given = 0;
	char *str = read_line(stdin);
	//size_t l = strlen(str);
	char *endp;
	cmd->a = parse_lineno(str, &endp, buf);
	if (endp != str) {
		str = endp;
		if (*str == ',') {
			str++;
			cmd->b = parse_lineno(str, &endp, buf);
			if (endp == str)
				return E_BAD_ADDR;
			str = endp;
		} else {
			cmd->b = cmd->a;
		}
		cmd->addr_given = 1;
	}
	cmd->cmd = *str++;
	switch (cmd->cmd) {
	case '\n':
	case 'n':
	case 'p':
	case 'h':
	case 'H':
	case 'q':
		break;
	default:
		return E_CMD;
	}
	if (*str != '\0' && *str != '\n')
		return E_BAD_CMD_SUFFIX;
	return E_NONE;
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
		return "Unknown command suffix";
	case E_UNEXP_ADDR:
		return "Unexpected address";
	case E_CMD:
		return "Unknown command";
	default:
		assert(0);
	}
}

static int buffer_validate_addr(struct buffer *buf, struct cmd *cmd)
{
	return cmd->a <= cmd->b
		&& cmd->a >= 1
		&& cmd->a <= buf->nlines
		&& cmd->b >= 1
		&& cmd->b <= buf->nlines;
}

int main(int argc, const char *argv[])
{
	if (argc != 2)
		xusage(1, "");
	const char *fname = argv[1];
	if (fname[0] == '-' && fname[1] != '\0')
		xusage(1, "illegal option -- %s\n", fname + 1);
	FILE *f = fopen(fname, "r");
	if (!f) {
		perror(fname);
		exit(1);
	}
	struct buffer buf;
	buffer_init_load(&buf, f);

	struct cmd cmd;
	enum err err, last_err = E_NONE;
	for (;;) {
		err = parse_command(&buf, &cmd);
		if (err != E_NONE)
			goto skip_cmd;
		if (!cmd.addr_given) {
			if (cmd.cmd == '\n')
				cmd.a = cmd.b = buf.cur_line + 1;
			else
				cmd.a = cmd.b = buf.cur_line;
		}
		if (!buffer_validate_addr(&buf, &cmd)) {
			err = E_BAD_ADDR;
			goto skip_cmd;
		}
		switch (cmd.cmd) {
		case '\n':
		case 'n':
		case 'p':
			buffer_print_range(&buf, cmd.a, cmd.b, cmd.cmd == 'n');
			buf.cur_line = cmd.b;
			break;
		case 'H':
			if (cmd.addr_given) {
				err = E_UNEXP_ADDR;
			} else {
				buf.print_errors = !buf.print_errors;
				if (buf.print_errors && last_err != E_NONE)
					puts(buf_err_str(last_err));
			}
			break;
		case 'h':
			if (cmd.addr_given)
				err = E_UNEXP_ADDR;
			else if (last_err != E_NONE)
				puts(buf_err_str(last_err));
			break; // TODO GCC BUG
		case 'q':
			goto quit;
		default:
			err = E_CMD;
		}	
skip_cmd:
		if (err != E_NONE) {
			puts("?");
			if (buf.print_errors)
				puts(buf_err_str(err));
			last_err = err;
		}
	}
quit:
	return err == E_NONE ? 0 : 1;
}
