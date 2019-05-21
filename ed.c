#include <assert.h>
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
};

static char *read_line(FILE *f)
{
	char buf[LINE_MAX_LEN];
	if (!fgets(buf, sizeof(buf), f)) {
		if (feof(f))
			return NULL;
		err(1, "fgets");
	}
	size_t len = strlen(buf);
	assert(len > 0);
	if (buf[len - 1] != '\n' && !feof(f))
		errx(1, "input line longer than %zu bytes", sizeof(buf));
	char *str = malloc(len + 1);
	if (!str)
		err(1, "malloc");
	strncpy(str, buf, len);
	str[len] = '\0';
	return str;
}

static struct line *load_buffer(FILE *f)
{
	size_t len = 0;
	struct line *first = NULL, *prev = NULL;
	char *text;
	while ((text = read_line(f)) != NULL) {
		if (len > LINE_MAX_LEN)
			errx(1, "input line too long at %zu bytes", len);
		struct line *l = malloc(sizeof(*l));
		if (!first)
			first = l;
		l->text = text;
		l->prev = prev;
		if (prev)
			prev->next = l;
		prev = l;
		l->next = NULL;
	}
	return first;
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
	struct line *line;
	for (line = load_buffer(f); line != NULL; line = line->next)
		printf("%s", line->text);
	return 0;
}
