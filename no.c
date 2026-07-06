/*
	# no
	*no*thing fancy. my cute text editor!

	# build:
	```sh
	cc -std=c89 -Wall -Wextra -Wpedantic -o no no.c
	```

	# goals
	- [ ] modal
	- [ ] search
	- [ ] undo / redo
	- [ ] range selection
	- [ ] per filetype config

	# implementation
	if i havent written this by the time im finished with the program you
	can remind me!!!

	# why the name?
	my favorite community college professor taught calculus III really well
	and something that he would always say after an explanation is
	"nothing fancy." math doesnt need to be hard. text editors dont need to
	be hard either!
*/

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#define CTRL_KEY(k) ((k) & 0x1f)

#define HEIGHT 24
#define WIDTH 80
#define GAP 256
#define LINES_GAP 256
#define VIEW_INIT 512
#define TAB 8

char *buf = NULL;
int buf_gap = 0;
int buf_start = 0;
int buf_size = 0;

struct termios cooked = { 0 };
struct termios raw = { 0 };

char *view = NULL;
int view_used = 0;
int view_size = 0;
int view_rows = 0;
int view_line = 0;

int *lines = NULL;
int lines_gap = 0;
int lines_start = 0;
int lines_size = 0;
int line = 0;
int col = 0;
int abs_col = 0;

void buf_init(int argc, char **argv) {
	FILE *f = NULL;
	long fsize = 0;

	if (argc != 2) {
		printf("file input please :3");
		exit(1);
	}

	f = fopen(argv[1], "r");

	if (f == NULL) {
		printf("invalid file :(");
		exit(1);
	}

	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	rewind(f);

	buf_start = 0;
	buf_gap = GAP;
	buf_size = fsize + buf_gap;
	buf = malloc(sizeof(char) * buf_size);
	fread(buf + buf_gap, fsize, 1, f);
	fclose(f);
}

char buf_get_char(int i) {
	if (i < buf_start) {
		return buf[i];
	} else {
		return buf[i + buf_gap];
	}
}

void buf_move_left(int n) {
	if (n > buf_start) {
		n = buf_start;
	}	
	
	memcpy(buf + buf_start + buf_gap - n, buf + buf_start - n, n);
	buf_start -= n;
}

void buf_move_right(int n) {
	if (n > buf_size - buf_gap - buf_start) {
		n = buf_size - buf_gap - buf_start;
	}

	memcpy(buf + buf_start, buf + buf_start + buf_gap, n);
	buf_start += n;
}

void buf_move_to(int n) {
	if (n < buf_start) {
		buf_move_left(buf_start - n);
	} else if (n > buf_start) {
		buf_move_right(n - buf_start);
	}
}

void buf_grow(int n) {
	int bump;
	char *dst;
	char *src;
	size_t size;

	if (buf_gap >= n) {
		return;
	}

	bump = ((n / GAP) + 1) * GAP;
	buf = realloc(buf, sizeof(char) * (buf_size + bump));
	src = buf + buf_start + buf_gap;
	dst = src + bump;
	size = buf_size - buf_start - buf_gap + 1;
	memcpy(dst, src, size);
	buf_gap += bump;
	buf_size += bump;
}

void buf_insert_char(char c) {
	buf_grow(1);
	buf[buf_start++] = c;
	buf_gap--;
}

int lines_get_line(int i) {
	if (i < lines_start) {
		return lines[i];
	} else {
		return lines[i + lines_gap];
	}
}

void lines_move_left(int n) {
	if (n > lines_start) {
		n = lines_start;
	}	
	
	memcpy(lines + lines_start + lines_gap - n, lines + lines_start - n, n);
	lines_start -= n;
}

void lines_move_right(int n) {
	if (n > lines_size - lines_gap - lines_start) {
		n = lines_size - lines_gap - lines_start;
	}

	memcpy(lines + lines_start, lines + lines_start + lines_gap, n);
	lines_start += n;
}

void lines_move_to(int n) {
	if (n < lines_start) {
		lines_move_left(lines_start - n);
	} else if (n > lines_start) {
		lines_move_right(n - lines_start);
	}
}

void lines_grow(int n) {
	int bump;
	int *dst;
	int *src;
	size_t size = 0;

	if (lines_gap >= n) {
		return;
	}

	bump = ((n / LINES_GAP) + 1) * LINES_GAP;
	lines = realloc(lines, sizeof(int) * (lines_size + bump));
	src = lines + lines_start + lines_gap;
	dst = src + bump;
	size = lines_size - lines_start - lines_gap + 1;
	memcpy(dst, src, size);
	lines_gap += bump;
	lines_size += bump;
}

void lines_insert(int i) {
	lines_grow(1);
	lines[lines_start++] = i;
	lines_gap--;
}

void lines_init(void) {
	int i = 0;

	lines_size = LINES_GAP;
	lines_gap = LINES_GAP;
	lines = malloc(sizeof(int) * lines_size);

	lines_insert(0);

	for (i = buf_gap; i < buf_size - 1; i++) {
		if (buf[i] == '\n') {
			lines_insert(i + 1 - buf_gap);
		}
	}

	if (buf[buf_size] == '\n') {
		lines_insert(i - buf_gap);
	}
}

int lines_get_len(int i) {
	if (i != lines_size - lines_gap) {
		return lines_get_line(i + 1) - lines_get_line(i);
	} else {
		return buf_size - buf_gap - lines_get_line(i);
	}
}

void view_init(void) {
	view_used = 0;
	view_size = VIEW_INIT;
	view = malloc(sizeof(char) * view_size);
}

void view_grow(int n) {
	if (view_used + n > view_size) {
		view_size *= 2;	
		view = realloc(view, sizeof(char) * view_size);
	}
}

void view_push(char *s, int n) {
	view_grow(n);
	memcpy(view + view_used, s, n);
	view_used += n;
}

void view_push_char(char c) {
	view_grow(1);
	view[view_used++] = c;
}

void view_write_char(char c) {
	if (c == '\n') {
		view_push("\r\n\x1b[K", 5);
		view_rows++;
	} else if (c == '\t') {
		int i;

		for (i = 0; i < TAB; i++) {
			view_push_char(' ');
		}
	} else {
		view_push_char(c);
	}
}

void view_write_cursor(void) {
	char cbuf[32];
	int len;
	int x = col + 1;
	int y = line - view_line;
	int i;
	int line_len = lines_get_line(line);

	for (i = line_len; i < line_len + col; i++) {
		if (buf_get_char(i) == '\t') {
			x += TAB - 1;
		}
	}

	len = snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%dH", y + 1, x);
	view_push(cbuf, len);
}

void view_write(void) {
	int i;

	view_used = 0;
	view_rows = 0;

	view_push("\x1b[H\x1b[K\x1b[?25l", 12);

	for (i = lines_get_line(view_line); i < buf_start && view_rows < HEIGHT; i++) {
		view_write_char(buf[i]);
	}

	for (i += buf_gap; i < buf_size && view_rows < HEIGHT; i++) {
		view_write_char(buf[i]);
	}

	view_write_cursor();

	view_push("\x1b[?25h", 6);
	write(STDOUT_FILENO, view, view_used);
}

void raw_mode_on(void) {
	tcgetattr(STDOUT_FILENO, &cooked);
	raw = cooked;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw);
}

void raw_mode_off(void) {
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &cooked);
}

void alt_buf_on(void) {
	write(STDOUT_FILENO, "\x1b[?1049h", 8);
}

void alt_buf_off(void) {
	write(STDOUT_FILENO, "\x1b[?1049l", 8);
}

void cursor_correct_col(void) {
	int line_len = lines_get_len(line) - 1;

	if (abs_col > line_len) {
		col = line_len;
	} else {
		col = abs_col;
	}
}

void cursor_down(void) {
	if (line < lines_size - lines_gap) {
		line++;
		cursor_correct_col();
	}

	if (line - view_line > HEIGHT) {
		view_line++;
	}
}

void cursor_up(void) {
	if (line > 0) {
		line--;
		cursor_correct_col();
	}

	if (line - view_line < 0) {
		view_line--;
	}
}

void cursor_right(void) {
	if (col < lines_get_len(line) - 1) {
		col++;
		abs_col = col;
	}
}

void cursor_left(void) {
	if (col != 0) {
		col--;
		abs_col = col;
	}
}

void cursor_beg(void) {
	col = 0;
	abs_col = col;
}

void cursor_end(void) {
	col = lines_get_len(line) - 1;
	abs_col = col;
}

void decrement_lines(void) {
	int i;

	for (i = line + 1; i < lines_start; i++) {
		lines[i]--;
	}

	for (i += lines_gap; i < lines_size; i++) {
		lines[i]--;
	}
}

void delete(void) {
	int cursor = lines_get_line(line) + col;
	
	if (buf_get_char(cursor) == '\n') {
		lines_move_to(line + 1);
		lines_gap++;
	}

	buf_move_to(cursor);
	buf_gap++;

	decrement_lines();
}

void backspace(void) {
	int cursor = lines_get_line(line) + col;

	if (line == 0 && col == 0) {
		return;
	}

	buf_move_to(cursor - 1);
	buf_gap++;

	if (col == 0) {
		col = lines_get_len(line - 1) - 1;
		abs_col = col;
		lines_move_to(line);
		lines_gap++;
		line--;
	} else {
		col--;
		abs_col = col;
	}

	decrement_lines();
}

void save(void) {
	FILE *f;

	f = fopen("TEST", "w");

	fwrite(buf, buf_start, 1, f);
	fwrite(buf + buf_start + buf_gap, buf_size - buf_start - buf_gap, 1, f);

	fclose(f);
}

void quit(void) {
	alt_buf_off();
	raw_mode_off();
	free(lines);
	free(view);
	free(buf);
	exit(0);
}

void insert_char(char c) {
	int i;

	buf_move_to(lines_get_line(line) + col);
	buf_insert_char(c);
	col++;

	for (i = line + 1; i < lines_start; i++) {
		lines[i]++;
	}

	for (i += lines_gap; i < lines_size; i++) {
		lines[i]++;
	}
}

void insert_line(void) {
	lines_move_to(line + 1);
	insert_char('\n');
	lines_insert(lines_get_line(line) + col);
	line++;
	col = 0;
}

void input(void) {
	char c;

	while (read(STDOUT_FILENO, &c, 1) == 0);

	switch (c) {
	case CTRL_KEY('n'):
		cursor_down();
		break;
	case CTRL_KEY('p'):
		cursor_up();
		break;
	case CTRL_KEY('f'):
		cursor_right();
		break;
	case CTRL_KEY('b'):
		cursor_left();
		break;
	case CTRL_KEY('a'):
		cursor_beg();
		break;
	case CTRL_KEY('e'):
		cursor_end();
		break;
	case CTRL_KEY('d'):
		delete();
		break;
	case 127: /* this is the backspace key. will find a better way to represent this later. */
		backspace();
		break;
	case 13: /* this is the enter key. will find a better way to represent this later. */
		insert_line();
		break;
	case CTRL_KEY('s'):
		save();
		break;
	case CTRL_KEY('q'):
		quit();
		break;
	default:
		insert_char(c);
		break;
	}
}

int main(int argc, char **argv) {
	buf_init(argc, argv);
	lines_init();
	view_init();
	raw_mode_on();
	alt_buf_on();

	while (1) {
		view_write();
		input();
	}

	return 0;
}
