/*
	*no*thing fancy. my cute text editor!

	cc -std=c89 -Wall -Wextra -Wpedantic -o no no.c

	- search
	- undo / redo
	- range selection
	- per filetype config
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)

#define GAP 256
#define LINES_GAP 256
#define VIEW_INIT 512
#define SEARCH_SIZE 64
#define STATUS_MAX 128
#define TAB 8

enum keys_extra {
	ENTER = 13,
	ESC = 27,
	BACKSPACE = 127,

	/*
		not reported by the terminal directly, so we just assign this to
		a random number
	*/
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL,
	HOME,
	END,
	PAGE_UP,
	PAGE_DOWN
};

enum status {
	NONE,
	SEARCH,
	WRITTEN,
	QUIT_CONFIRM
};

char *filename = NULL;

char *buf = NULL;
int buf_gap = 0;
int buf_pos = 0;
int buf_size = 0;

struct termios cooked = { 0 };
struct termios raw = { 0 };

char *view = NULL;
int view_used = 0;
int view_size = 0;
int view_rows = 0;
int view_cols = 0;
int view_line = 0;

int *lines = NULL;
int lines_gap = 0;
int lines_pos = 0;
int lines_size = 0;
int line = 0;
int col = 0;
int abs_col = 0;

struct winsize ws = { 0 };

enum status status = NONE;

int searching = 0;
char search[SEARCH_SIZE] = { 0 };
int search_used = 0;
int search_cursor = 0;

int written = 0;
int quit_confirm = 0;

void buf_init(int argc, char **argv) {
	FILE *f = NULL;
	long fsize = 0;

	if (argc != 2) {
		printf("file input please :3");
		exit(EXIT_FAILURE);
	}

	filename = argv[1];
	f = fopen(filename, "r");

	if (f == NULL) {
		printf("invalid file :(");
		exit(EXIT_FAILURE);
	}

	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	rewind(f);

	buf_pos = 0;
	buf_gap = GAP;
	buf_size = fsize + buf_gap;
	buf = malloc(sizeof(char) * buf_size);
	fread(buf + buf_gap, fsize, 1, f);
	fclose(f);
}

char buf_get_char(int i) {
	if (i < buf_pos) {
		return buf[i];
	} else {
		return buf[i + buf_gap];
	}
}

void buf_move_left(int n) {
	int src = 0;
	int dst = 0;

	if (n > buf_pos) {
		n = buf_pos;
	}	

	src = buf_pos - n;
	dst = src + buf_gap;
	memcpy(&buf[dst], &buf[src], n * sizeof(char));
	buf_pos -= n;
}

void buf_move_right(int n) {
	int src = 0;
	int dst = 0;

	if (n > buf_size - buf_gap - buf_pos) {
		n = buf_size - buf_gap - buf_pos;
	}

	src = buf_pos + buf_gap;
	dst = buf_pos;
	memcpy(&buf[dst], &buf[src], n * sizeof(char));
	buf_pos += n;
}

void buf_move(int n) {
	if (n < buf_pos) {
		buf_move_left(buf_pos - n);
	} else if (n > buf_pos) {
		buf_move_right(n - buf_pos);
	}
}

void buf_grow(int n) {
	int bump = 0;
	int dst = 0;
	int src = 0;
	int size = 0;

	if (buf_gap >= n) {
		return;
	}

	bump = (((n - buf_gap) / GAP) + 1) * GAP;
	buf = realloc(buf, sizeof(char) * (buf_size + bump));
	src = buf_pos + buf_gap;
	dst = src + bump;
	size = buf_size - buf_pos - buf_gap;
	memcpy(&buf[dst], &buf[src], size * sizeof(char));
	buf_gap += bump;
	buf_size += bump;
}

void buf_insert_char(char c) {
	buf_grow(1);
	buf[buf_pos++] = c;
	buf_gap--;
}

int lines_get(int i) {
	if (i < lines_pos) {
		return lines[i];
	} else {
		return lines[i + lines_gap];
	}
}

void lines_move_left(int n) {
	int src = 0;
	int dst = 0;

	if (n > lines_pos) {
		n = lines_pos;
	}	
	
	src = lines_pos - n;
	dst = src + lines_gap;
	memcpy(&lines[dst], &lines[src], n * sizeof(int));
	lines_pos -= n;
}

void lines_move_right(int n) {
	int src = 0;
	int dst = 0;

	if (n > lines_size - lines_gap - lines_pos) {
		n = lines_size - lines_gap - lines_pos;
	}

	src = lines_pos + lines_gap;
	dst = lines_pos;
	memcpy(&lines[dst], &lines[src], n * sizeof(int));
	lines_pos += n;
}

void lines_move(int n) {
	if (n < lines_pos) {
		lines_move_left(lines_pos - n);
	} else if (n > lines_pos) {
		lines_move_right(n - lines_pos);
	}
}

void lines_grow(int n) {
	int bump = 0;
	int dst = 0;
	int src = 0;
	int size = 0;

	if (lines_gap >= n) {
		return;
	}

	bump = (((n - lines_gap) / LINES_GAP) + 1) * LINES_GAP;
	lines = realloc(lines, sizeof(int) * (lines_size + bump));
	src = lines_pos + lines_gap;
	dst = src + bump;
	size = lines_size - lines_pos - lines_gap;
	memcpy(&lines[dst], &lines[src], size * sizeof(int));
	lines_gap += bump;
	lines_size += bump;
}

void lines_insert(int i) {
	lines_grow(1);
	lines[lines_pos++] = i;
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
	if (i != lines_size - lines_gap - 1) {
		return lines_get(i + 1) - lines_get(i);
	} else {
		return buf_size - buf_gap - lines_get(i);
	}
}

int nearest_line_index(int l) {
	int min = 0;
	int max = lines_size - 1;
	int i = (min + max) / 2;

	while (max - min > 1) {
		i = (min + max) / 2;

		if (lines[i] > l) {
			max = i;
		} else if (lines[i] < l) {
			min = i;
		} else {
			return i;	
		}
	}

	return min;
}

void search_clear(void) {
	search_used = 0;
	search_cursor = 0;
}

/* memmem before the gap */
void search_prev(void) {
/*
	int i = 0;
	char *ptr = memmem(&buf, buf_pos, );

	
*/
}
/* memmem after the gap */
void search_next(void) {
	char *ptr = memmem(&buf[buf_pos + buf_gap + search_used], buf_size - buf_gap - buf_pos - search_used, search, search_used);
	int i = 0;

	if (ptr == NULL) {
		return;
	}

	i = ptr - buf - buf_gap;
	line = nearest_line_index(i);
	view_line = line;
	col = i - lines_get(line);
	abs_col = col;
	buf_move(lines_get(line) + col);
}

void search_cursor_left(void) {
	if (search_cursor > 0) {
		--search_cursor;
	}
}

void search_cursor_right(void) {
	if (search_cursor < search_used) {
		++search_cursor;
	}
}

void search_cursor_home(void) {
	search_cursor = 0;
}

void search_cursor_end(void) {
	search_cursor = search_used;
}

void search_delete(void) {
	if (search_cursor == search_used) {
		return;
	}

	memcpy(
		&search[search_cursor],
		&search[search_cursor + 1],
		(search_used - search_cursor - 1) * sizeof(char)
	);
	--search_used;
}

void search_backspace(void) {
	if (search_cursor == 0) {
		return;
	}

	--search_cursor;
	search_delete();
}

void search_insert(int c) {
	if (search_used >= SEARCH_SIZE || !isprint(c)) {
		return;
	}

	memcpy(
		&search[search_cursor + 1],
		&search[search_cursor],
		(search_used - search_cursor) * sizeof(char)
	);
	search[search_cursor++] = c;
	++search_used;
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
	memcpy(view + view_used, s, n * sizeof(char));
	view_used += n;
}

void view_push_char(char c) {
	view_grow(1);
	view[view_used++] = c;
}

void view_write_char(char c) {
	if (c == '\n') {
		view_push("\r\n", 2);
		++view_rows;
	} else if (c == '\t') {
		int i;

		for (i = 0; i < TAB; i++) {
			view_push_char(' ');
		}
	} else if (!isprint(c)) {
		view_push_char('?');
	} else {
		view_push_char(c);
	}
}

void view_write_status(void) {
	switch (status) {
	case NONE:
		break;
	case SEARCH:
		view_push("SEARCH: ", 8);
		view_push(search, search_used);
		break;
	case WRITTEN:
		view_push("written", 7);
		break;
	case QUIT_CONFIRM:
		view_push("unsaved changes, press again to quit!", 37);
		break;
	}
}

void view_write_cursor_normal(void) {
	char cbuf[32] = { 0 };
	int len = 0;
	int x = col + 1;
	int y = line - view_line;
	int i = 0;
	int line_len = lines_get(line);

	for (i = line_len; i < line_len + col; i++) {
		if (buf_get_char(i) == '\t') {
			x += TAB - 1;
		}
	}

	len = snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%dH", y + 1, x);
	view_push(cbuf, len);
}

void view_write_cursor_search(void) {
	char cbuf[32] = { 0 };
	int len = 0;
	int y = ws.ws_row;
	int x = 8 + search_cursor + 1;

	len = snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%dH", y, x);
	view_push(cbuf, len);
}

void view_write_cursor(void) {
	if (searching) {
		view_write_cursor_search();
	} else {
		view_write_cursor_normal();
	}
}

void view_write(void) {
	int i;

	view_used = 0;
	view_rows = 0;

	view_push("\x1b[H\x1b[2J", 7);

	for (i = lines_get(view_line); i < buf_pos && view_rows < ws.ws_row - 1; i++) {
		view_write_char(buf[i]);
	}

	for (i += buf_gap; i < buf_size && view_rows < ws.ws_row - 1; i++) {
		view_write_char(buf[i]);
	}

	while (view_rows < ws.ws_row - 1) {
		view_push("\r\n\x1b[K", 5);
		view_rows++;
	}

	view_write_status();
	view_write_cursor();

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

void ws_update(void) {
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
}

void ws_signal_update(int _) {
	ws_update();
	view_write();
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
	if (line < lines_size - lines_gap - 1) {
		line++;
		cursor_correct_col();
	}

	if (line - view_line > ws.ws_row - 2) {
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

void cursor_home(void) {
	col = 0;
	abs_col = col;
}

void cursor_end(void) {
	col = lines_get_len(line) - 1;
	abs_col = col;
}

void cursor_page_up(void) {
	int i;

	for (i = 0; i < (ws.ws_row - 1) * 2 - 1; i++) {
		cursor_up();
	}
}

void cursor_page_down(void) {
	int i;

	for (i = 0; i < (ws.ws_row - 1) * 2 - 1; i++) {
		cursor_down();
	}
}

void decrement_lines(void) {
	int i;

	for (i = line + 1; i < lines_pos; ++i) {
		--lines[i];
	}

	for (i += lines_gap; i < lines_size; ++i) {
		--lines[i];
	}
}

void delete(void) {
	int cursor = lines_get(line) + col;

	if (cursor + buf_gap >= buf_size) {
		return;
	}

	written = 0;

	if (buf_get_char(cursor) == '\n') {
		lines_move(line + 1);
		lines_gap++;
	}

	buf_move(cursor);
	buf_gap++;

	decrement_lines();
}

void delete_to_end_of_line(void) {
	int i = 0;
	int n = lines_get_len(line) - col - 1;

	for (i = 0; i < n; i++) {
		delete();
	}
}

void backspace(void) {
	int cursor = lines_get(line) + col;

	if (line == 0 && col == 0) {
		return;
	}

	written = 0;

	buf_move(cursor - 1);
	buf_gap++;

	if (col == 0) {
		col = lines_get_len(line - 1) - 1;
		abs_col = col;
		lines_move(line);
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

	f = fopen(filename, "w");

	fwrite(buf, buf_pos, 1, f);
	fwrite(buf + buf_pos + buf_gap, buf_size - buf_pos - buf_gap, 1, f);

	fclose(f);

	written = 1;
	quit_confirm = 0;
	status = WRITTEN;
}

void quit(void) {
	if (!written && !quit_confirm) {
		quit_confirm = 1;
		status = QUIT_CONFIRM;
	} else {
		exit(EXIT_SUCCESS);
	}
}

void exiting(void) {
	alt_buf_off();
	raw_mode_off();
	free(lines);
	free(view);
	free(buf);
}

void insert_char(char c) {
	int i;

	written = 0;

	buf_move(lines_get(line) + col);
	buf_insert_char(c);
	col++;

	for (i = line + 1; i < lines_pos; i++) {
		lines[i]++;
	}

	for (i += lines_gap; i < lines_size; i++) {
		lines[i]++;
	}
}

void insert_line(void) {
	insert_char('\n');
	lines_move(line + 1);
	lines_insert(lines_get(line) + col);
	cursor_down();
	cursor_home();
}

int input_process(void) {
	char c = '\0';
	char seq[3] = { 0 };

	while (read(STDIN_FILENO, &c, 1) == 0);

	if (c != ESC) {
		return c;
	}

	/* put this in a while loop because we dont know how many characters can be in an escape sequence */
	while (1) {
		/* just incase this simply is an ESC input */
		if (read(STDIN_FILENO, &seq[0], 1) == 0) {
			return ESC;
		} else if (read(STDIN_FILENO, &seq[1], 1) == 0) {
			return ESC;
		}

		if (seq[0] != '[') {
			continue;
		}

		/* extended escape sequence! */
		if (seq[1] >= '0' && seq[1] <= '9') {
			if (read(STDIN_FILENO, &seq[2], 1) == 0) {
				return ESC;
			}

			if (seq[2] != '~') {
				continue;
			}

			switch (seq[1]) {
			case '3': return DEL;
			case '5': return PAGE_UP;
			case '6': return PAGE_DOWN;
			}
		} else {
			switch (seq[1]) {
			case 'A': return ARROW_UP;
			case 'B': return ARROW_DOWN;
			case 'C': return ARROW_RIGHT;
			case 'D': return ARROW_LEFT;
			case 'H': return HOME;
			case 'F': return END;
			}
		}
	}
}

void input_normal(int c) {
	switch (c) {
	case ARROW_DOWN:
		cursor_down();
		break;
	case ARROW_UP:
		cursor_up();
		break;
	case ARROW_RIGHT:
		cursor_right();
		break;
	case ARROW_LEFT:
		cursor_left();
		break;
	case HOME:
		cursor_home();
		break;
	case END:
		cursor_end();
		break;
	case PAGE_UP:
		cursor_page_up();
		break;
	case PAGE_DOWN:
		cursor_page_down();
		break;
	case BACKSPACE:
		backspace();
		break;
	case DEL:
		delete();
		break;
	case ENTER:
		insert_line();
		break;
	case CTRL_KEY('d'):
		delete();
		break;
	case CTRL_KEY('k'):
		delete_to_end_of_line();
		break;
	case CTRL_KEY('f'):
		search_clear();
		searching = 1;
		status = SEARCH;
		break;
	case CTRL_KEY('s'):
		save();
		break;
	case CTRL_KEY('q'):
		quit();
		break;
	case '\0':
		break;
	default:
		insert_char(c);
		break;
	}
}

void input_search(int c) {
	switch (c) {
	case ESC:
	case ENTER:
		searching = 0;
		status = NONE;
		break;
	case ARROW_UP:
		search_prev();
		break;
	case ARROW_DOWN:
		search_next();
		break;
	case ARROW_LEFT:
		search_cursor_left();
		break;
	case ARROW_RIGHT:
		search_cursor_right();
		break;
	case HOME:
		search_cursor_home();
		break;
	case END:
		search_cursor_end();
		break;
	case DEL:
		search_delete();
		break;
	case BACKSPACE:
		search_backspace();
		break;
	default:
		search_insert(c);
		break;
	}
}

void input(void) {
	int c = input_process();

	if (searching) {
		input_search(c);
	} else {
		input_normal(c);
	}
}

int main(int argc, char **argv) {
	buf_init(argc, argv);
	lines_init();
	view_init();
	raw_mode_on();
	alt_buf_on();
	ws_update();

	atexit(exiting);
	signal(SIGWINCH, ws_signal_update);

	while (1) {
		view_write();
		input();
	}

	return EXIT_SUCCESS;
}
