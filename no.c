/*
 * *no*thing fancy. my cute text editor!
 *
 * cc -std=c89 -Wall -Wextra -Wpedantic -o no no.c
 * 
 * recreational text editor for learning gap buffers. original goal was to make
 * something id actually use but since bram moolenaar is a fucking genius vim
 * is *too good* and all my needs were already met. i have learned enough from
 * this project, so i am satisfied, and will leave it as an educational
 * resource. start from the `main` function and follow the trail of functions
 * from there.
 *
 * all the code is one file for convenience and because of heavy global
 * variable usage. i use globals because we are only concerned with one file,
 * so you only need one of every useful data structure.
 * 
 * abstraction layout:
 * 
 * text editor
 * + text gap buffer
 * + lines gap buffer
 * + view
 * + search
 * + input
 * 
 * theres only one "layer" of abstractions because of heavy global usage and
 * because coupling makes me sad. in my opinion the code is straightforward to
 * read but of course a mother will always think fondly of her child.
 * 
 * why use a gap buffer? ropes, piece tables, and any of their descendants just
 * seemed like a headache to implement. a dynamic array of dynamic strings was
 * also a nice candidate, but at the time i did not want to allocate each
 * string (which is a rather silly concern in hindsight). i also just found gap
 * buffers beautiful and wanted to play with them :)
 * 
 * gap buffers make linear text searching and file loading easier, but now
 * everything user facing becomes difficult because you have to track the
 * indices of newlines. this is where using a gap buffer falls apart because
 * now there is friction between your mental model of the text contents
 * (i.e. an array of lines) and how its stored (i.e. an array of characters). 
 * if youre planning to attempt a text editor i advise that you use a dynamic
 * array of dynamic strings since i assume youre making this on a regular
 * computer and because it works fine for text editors like Kakoune. chances
 * are that the REAL bottleneck of your program would be within I/O or graphics
 * if youre into that.
 * 
 * that being said, emacs works just fine with a gap buffer, so nothing is
 * stopping you from using that either! do better than i did, please!
 *
 * in terms of like. "licensing",,, just treat this as public domain. just use
 * the code. dont even worry about it. credit me if you want. that would be
 * pretty nice actually.
 *
 * have fun!~~~~
 *
 * - sylvia
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>

/*
 * the hexadecimal value 0x1f is equal to the binary value 00011111
 *
 * example: CTRL_KEY('a')
 * according to `man ascii`, 'a' = (binary) 01100001
 * thus,
 *
 * CTRL_KEY('a') = 01100001 & 0x1f = 01100001 & 00011111
 *
 * applying the bitwise AND operation results in
 *
 *     01100001
 * AND 00011111
 * ------------
 * =   00000001
 *
 * this value refers to the first ASCII control character after the NULL char,
 * and when you press `Control + a` in the terminal it will send 00000001 to
 * stdin.
 *
 * example: CTRL_KEY('A')
 * 'A' = 01000001
 *
 *     01000001
 * AND 00011111
 * ------------
 * =   00000001
 *
 * as CTRL_KEY('A') = CTRL_KEY('a'), we sadly cant support the
 * shift + control modifier :(
 */
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
	 * not reported by the terminal directly, so we just assign this to a
	 * number far away from what is included in any ASCII character sets
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

/*
 * as simple as you can really get in terms of file initialization.
 * just allocate the file size + initial gap and then "paste" the contents
 * after the gap so that the user can begin editing at the very beginning of
 * the file.
 *
 * gap buffers make insertion into the middle of arrays easy
 *
 * the transformation:
 *
 * caca => capoopca
 *
 * can be visualized in a gap buffer as follows:
 *
 * c a c a _ _ _ _
 *         ^ pos = 4 (this is an index in the array)
 *         ^ ^ ^ ^ gap = 4 (ignored then overwritten indices)
 *
 * move the gap 2 chars to the left
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * and finally insert
 *
 * c a p o o p c a
 *             ^ pos = 6
 *             gap = 0
 *
 * each function will have its own explanation. have fun!
 */
void buf_init(int argc, char **argv)
{
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

/*
 * an alternative method of indexing that accounts for the gap
 *
 * for example, if you try to directly index the third char in a gap buffer:
 *
 * c a _ _ _ _ c a
 *     ^
 *
 * you will be accessing inside of the gap and get a "junk" value. we add the
 * current gap length to account for this to get the third OCCUPIED character
 * c a _ _ _ _ c a
 *             ^
 */
char buf_get_char(int i)
{
	return buf[i < buf_pos ? i : i + buf_gap];
}

/*
 * changes the point of insertion
 *
 * c a c a _ _ _ _
 *         ^ pos = 4
 *         ^ ^ ^ ^ gap = 4
 *
 * we want to move two chars to the left
 *
 * copy the required memory
 *
 * c a c a _ _ c a
 *         ^ pos = 4
 *         ^ ^ ^ ^ gap = 4
 *
 * do some arithmetic
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 */
void buf_move_left(int n)
{
	int src = 0;
	int dst = 0;

	if (n > buf_pos)
		n = buf_pos;

	src = buf_pos - n;
	dst = src + buf_gap;
	memcpy(&buf[dst], &buf[src], n * sizeof(char));
	buf_pos -= n;
}

/*
 * essentiallly the same as above
 *
 * p.s. pos + gap isnt overcounting by 1 as pos is 0-indexed :D
 */
void buf_move_right(int n)
{
	int src = 0;
	int dst = 0;

	if (n > buf_size - buf_gap - buf_pos)
		n = buf_size - buf_gap - buf_pos;

	src = buf_pos + buf_gap;
	dst = buf_pos;
	memcpy(&buf[dst], &buf[src], n * sizeof(char));
	buf_pos += n;
}

/*
 * moves point of insertion BEFORE index `n`
 *
 * c a c a _ _ _ _
 *         ^ pos = 4
 *         ^ ^ ^ ^ gap = 4
 *
 * a move to index 1 leads to
 *
 * c _ _ _ _ a c a
 *   ^ pos = 1
 *   ^ ^ ^ ^ gap = 4
 */
void buf_move(int n)
{
	if (n < buf_pos)
		buf_move_left(buf_pos - n);
	else if (n > buf_pos)
		buf_move_right(n - buf_pos);
}

/*
 * the growth strategy i use is just adding to the existing size
 *
 * this ensures that the gap is always less than or equal to `GAP`
 *
 * the bump value is set in a rather confusing way but it makes more sense
 * once you realize we are using integer division
 *
 * note that pos - gap is not undercounting by 1 as pos is 0-indexed :D
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * c a p o o p c a
 *             ^ pos = 6
 *             gap = 0
 *
 * now onto regrowth:
 *
 * 1. reallocation
 *
 * c a p o o p c a _ _ _ _ _ _ _ _
 *             ^ pos = 6
 *             gap = 0
 *
 * 2. moving
 *
 * c a p o o p _ _ _ _ _ _ _ _ c a
 *             ^ pos = 6
 *             gap = 0
 *
 * 3. adjusting values
 *
 * c a p o o p _ _ _ _ _ _ _ _ c a
 *             ^ pos = 6
 *             gap = 8
 *
 * ok now you can insert again :)
 */
void buf_grow(int n)
{
	int bump = 0;
	int dst = 0;
	int src = 0;
	int size = 0;

	if (buf_gap >= n)
		return;

	bump = (((n - buf_gap) / GAP) + 1) * GAP;
	buf = realloc(buf, sizeof(char) * (buf_size + bump));
	src = buf_pos + buf_gap;
	dst = src + bump;
	size = buf_size - buf_pos - buf_gap;
	memcpy(&buf[dst], &buf[src], size * sizeof(char));
	buf_gap += bump;
	buf_size += bump;
}

/*
 * before:
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * after:
 *
 * c a p _ _ _ c a
 *       ^ pos = 3
 *       ^ ^ ^ gap = 3
 */
void buf_insert_char(char c)
{
	buf_grow(1);
	buf[buf_pos++] = c;
	buf_gap--;
}

int lines_get(int i)
{
	return lines[i < lines_pos ? i : i + lines_gap];
}

void lines_move_left(int n)
{
	int src = 0;
	int dst = 0;

	if (n > lines_pos)
		n = lines_pos;
	
	src = lines_pos - n;
	dst = src + lines_gap;
	memcpy(&lines[dst], &lines[src], n * sizeof(int));
	lines_pos -= n;
}

void lines_move_right(int n)
{
	int src = 0;
	int dst = 0;

	if (n > lines_size - lines_gap - lines_pos)
		n = lines_size - lines_gap - lines_pos;

	src = lines_pos + lines_gap;
	dst = lines_pos;
	memcpy(&lines[dst], &lines[src], n * sizeof(int));
	lines_pos += n;
}

void lines_move(int n)
{
	if (n < lines_pos)
		lines_move_left(lines_pos - n);
	else if (n > lines_pos)
		lines_move_right(n - lines_pos);
}

void lines_grow(int n)
{
	int bump = 0;
	int dst = 0;
	int src = 0;
	int size = 0;

	if (lines_gap >= n)
		return;

	bump = (((n - lines_gap) / LINES_GAP) + 1) * LINES_GAP;
	lines = realloc(lines, sizeof(int) * (lines_size + bump));
	src = lines_pos + lines_gap;
	dst = src + bump;
	size = lines_size - lines_pos - lines_gap;
	memcpy(&lines[dst], &lines[src], size * sizeof(int));
	lines_gap += bump;
	lines_size += bump;
}

void lines_insert(int i)
{
	lines_grow(1);
	lines[lines_pos++] = i;
	lines_gap--;
}

/*
 * we store an integer gap buffer of indices. keep in mind that these are the
 * indices AFTER the newline characters
 *
 * example:
 * buf = "hello\nworld\nhi\nhi\nhi!!!"
 *               ^      ^   ^   ^
 * lines = { 6, 12, 15, 18 }
 *
 * for an explanation of what gap buffers are, refer to `buf_init`
 *
 * the reason we want the index AFTER the newline character is because we
 * usually dont care about the newline character itself and dont want to
 * interact with them for operations such as deletion and traversal
 */
void lines_init(void)
{
	int i = 0;

	lines_size = LINES_GAP;
	lines_gap = LINES_GAP;
	lines = malloc(sizeof(int) * lines_size);

	lines_insert(0);

	for (i = buf_gap; i < buf_size - 1; ++i) {
		if (buf[i] == '\n')
			lines_insert(i + 1 - buf_gap);
	}

	if (buf[buf_size] == '\n')
		lines_insert(i - buf_gap);
}

/*
 * the only special part of this is getting the length of the LAST line.
 *
 * 1. we negate by 1 in the if statement since we want to account for
 *    0-indexing. you have no idea how long i spent on this mistake.
 * 2. for the else branch we effectively negate the index of the final
 *    OCCUPIED character
 */
int lines_get_len(int i)
{
	if (i != lines_size - lines_gap - 1)
		return lines_get(i + 1) - lines_get(i);
	else
		return buf_size - buf_gap - lines_get(i);
}

/*
 * reimplement as binary search maybe? probably not that needed though
 */
int nearest_line_index(int l)
{
	int i = 0;

	for (i = 0; i < lines_pos; ++i) {
		if (lines[i] > l)
			return i - 1;
	}

	for (i += lines_gap; i < lines_size; ++i) {
		if (lines[i] > l)
			return i - 1 - lines_gap;
	}

	return 0;
}

void search_clear(void)
{
	search_used = 0;
	search_cursor = 0;
}

/*
 * i just straight up didnt implement this LOL
 *
 * this is harder than going to the next occurrence because going backwards
 * means you have to scan the first half and then go to the last found index.
 */
void search_prev(void)
{
	
}

/*
 * this is satisfying with gap buffers since we can just scan the second half
 *
 * `memmem` isnt part of the C standard but is rather a GNU extension.
 * im fine with this since this wasnt meant to be cross platform anyways and
 * because strstr would require us to convert the needle and haystack into
 * c strings which would literally make me so angry that i could burn AT&T
 * to the ground.
 */
void search_next(void)
{
	char *ptr = memmem(
		&buf[buf_pos + buf_gap + search_used],
		buf_size - buf_gap - buf_pos - search_used,
		search,
		search_used
	);
	int i = 0;

	if (ptr == NULL)
		return;

	i = ptr - buf - buf_gap;
	line = nearest_line_index(i);
	view_line = line;
	col = i - lines_get(line);
	abs_col = col;
	buf_move(lines_get(line) + col);
}

void search_cursor_left(void)
{
	if (search_cursor > 0)
		--search_cursor;
}

void search_cursor_right(void)
{
	if (search_cursor < search_used)
		++search_cursor;
}

void search_cursor_home(void)
{
	search_cursor = 0;
}

void search_cursor_end(void)
{
	search_cursor = search_used;
}

void search_delete(void)
{
	if (search_cursor == search_used)
		return;

	memcpy(
		&search[search_cursor],
		&search[search_cursor + 1],
		(search_used - search_cursor - 1) * sizeof(char)
	);
	--search_used;
}

void search_backspace(void)
{
	if (search_cursor == 0)
		return;

	--search_cursor;
	search_delete();
}

void search_insert(int c)
{
	if (search_used >= SEARCH_SIZE || !isprint(c))
		return;

	memcpy(
		&search[search_cursor + 1],
		&search[search_cursor],
		(search_used - search_cursor) * sizeof(char)
	);
	search[search_cursor++] = c;
	++search_used;
}

/*
 * implementation justifications within `view_write`
 */
void view_init(void)
{
	view_used = 0;
	view_size = VIEW_INIT;
	view = malloc(view_size * sizeof(char));
}

void view_grow(int n)
{
	if (view_used + n > view_size) {
		view_size *= 2;	
		view = realloc(view, sizeof(char) * view_size);
	}
}

void view_push(char *s, int n)
{
	view_grow(n);
	memcpy(view + view_used, s, n * sizeof(char));
	view_used += n;
}

void view_push_char(char c)
{
	/* TODO: THIS IS VERY STUPID */
	if (view_cols >= ws.ws_col)
		return;

	view_grow(1);
	view[view_used++] = c;
	++view_cols;
}

/*
 * as previously mentioned, we have to reconcile the "flat" layout of the gap
 * buffer with the "2d" layout of a terminal display.
 *
 * we cant just print both halves of the gap buffer as is because
 * 1. each new line MUST be followed by a carriage return, otherwise the output
 *    will end up
 *                just like this
 *                               every time you enter a new line.
 * 2. tabs MUST be rendered as 8 or so separate spaces because it would fuck
 *    with the cursor column position by being 8 - 1 characters off >:(
 * 3. we have to handle cases where the user inputs unprintable characters,
 *    such as control characters.
 */
void view_write_char(char c)
{
	if (c == '\n') {
		view_push("\r\n", 2);
		++view_rows;
		view_cols = 0;
	} else if (c == '\t') {
		int i;

		for (i = 0; i < TAB; ++i)
			view_push_char(' ');
	} else if (!isprint(c)) {
		view_push_char('?');
	} else {
		view_push_char(c);
	}
}

void view_write_status(void)
{
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

void view_write_cursor_normal(void)
{
	char cbuf[32] = { 0 };
	int len = 0;
	int x = col + 1;
	int y = line - view_line;
	int i = 0;
	int line_len = lines_get(line);

	for (i = line_len; i < line_len + col; ++i) {
		if (buf_get_char(i) == '\t')
			x += TAB - 1;
	}

	/*
	 * \x1b[10;11H sends the cursor to the 10th row and 11th column.
	 *
	 * keep in mind that values start from 1 and not 0.
	 */
	len = snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%dH", y + 1, x);
	view_push(cbuf, len);
}

void view_write_cursor_search(void)
{
	char cbuf[32] = { 0 };
	int len = 0;
	int y = ws.ws_row;
	int x = 8 + search_cursor + 1;

	len = snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%dH", y, x);
	view_push(cbuf, len);
}

void view_write_cursor(void)
{
	if (searching)
		view_write_cursor_search();
	else
		view_write_cursor_normal();
}

/*
 * doing individual calls to `write` proved to be noticably slow. so, we create
 * a dynamic string, append to it, and write it all at once. 
 *
 * most of the heavy lifting is done by the `view_write_char` function
 *
 * for the `view` itself, its nothing fancy. the growth strategy is just
 * doubling the capacity once you reach it.
 *
 * there are some magic strings present in the following code. these are called
 * ANSI escape sequences. ill explain these as we go, but i decided against
 * making fancy macros for these because `view_push` also has a length
 * parameter and i just dont want to deal with that lmao
 */
void view_write(void)
{
	int i = lines_get(view_line);

	view_used = 0;
	view_rows = 0;

	/*
	 * \x1b[H brings the cursor to the top left
	 * \x1b[2J clears the entire screen
 	 */
	view_push("\x1b[H\x1b[2J", 7);

	for (; i < buf_pos && view_rows < ws.ws_row - 1; ++i)
		view_write_char(buf[i]);

	for (i += buf_gap; i < buf_size && view_rows < ws.ws_row - 1; ++i)
		view_write_char(buf[i]);

	while (view_rows < ws.ws_row - 1) {
		view_push("\r\n", 2);
		view_rows++;
	}

	view_write_status();
	view_write_cursor();

	write(STDOUT_FILENO, view, view_used);
}

/*
 * terminals often run in canonical mode. input gets sent in lines and you can
 * send signals like Control + c to cancel the process or Control + z to
 * suspend it. setting the terminal to "raw" mode gives you more control
 * over... well basically everything.
 *
 * in the case of our text editor, we want "real time" input by sending it
 * per-character and disabling the signals.
 *
 * disable BRKINT: ignore break conditions
 * disable ICRNL: do not convert carriage return '\r' characters to
 *                 newline '\n' characters
 * disable INPCK: disable input parity checking,
 *                i.e. an extra "error detection" bit is not added to input
 * disable ISTRIP: do not strip the 8th bit off chars
 * disable IXON: disable output flow control
 *               i.e. disables Control + s for pausing output
 *                    and Control + q for resuming
 * disable OPOST: disable all output processing
 * enable CS8: set the byte size to 8 bits
 * disable ECHO: dont automatically print user input
 * disable ICANON: disables input line "canonicalization". this effects a shit
 *                 ton of other things so go `man termios`
 * disable IEXTEN: disables Control + v / r / w.
 * disable ISIG: disables Control + c / z
 * VMIN: the minimum number of bytes required to push input. set to 0 so that
 *       we push every 0.1 * VTIME seconds. if there is no input by that time
 *       it just gives 0.
 */
void raw_mode_on(void)
{
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

void raw_mode_off(void)
{
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &cooked);
}

/*
 * fancy terminal feature which clears the screen AND prior terminal scrollback
 * so that rendering doesnt become awkward.
 */
void alt_buf_on(void)
{
	write(STDOUT_FILENO, "\x1b[?1049h", 8);
}

void alt_buf_off(void)
{
	write(STDOUT_FILENO, "\x1b[?1049l", 8);
}

/*
 * `ioctl` is used to control hardware devices, and in this case we want to
 * "control" our terminal. in reality we just use this to get the
 * "screen size" of our terminal. i.e. how many rows and columns we have.
 * we gotta adapt to changing screen sizes yaknow
 */
void ws_update(void)
{
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
}

void ws_signal_update(int _)
{
	ws_update();
	view_write();
}

/*
 * so, this is a subtle feature you may have noticed from your text editor
 *
 * put your cursor at the end of this line
 * go down
 * go down again
 * aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
 *
 * notice that once you go down to the 4th line, your cursor is on the same
 * column as the end of the first line?
 *
 * this is essentially the correction that this function is doing.
 */
void cursor_correct_col(void)
{
	int line_len = lines_get_len(line) - 1;
	col = abs_col > line_len ? line_len : abs_col;
}

void cursor_down(void)
{
	if (line < lines_size - lines_gap - 1) {
		++line;
		cursor_correct_col();
	}

	if (line - view_line > ws.ws_row - 2)
		++view_line;
}

void cursor_up(void)
{
	if (line > 0) {
		line--;
		cursor_correct_col();
	}

	if (line - view_line < 0)
		--view_line;
}

void cursor_right(void)
{
	if (col < lines_get_len(line) - 1) {
		col++;
		abs_col = col;
	}
}

void cursor_left(void)
{
	if (col != 0) {
		col--;
		abs_col = col;
	}
}

void cursor_home(void)
{
	col = 0;
	abs_col = col;
}

void cursor_end(void)
{
	col = lines_get_len(line) - 1;
	abs_col = col;
}

/*
 * im going to be completely honest with you and tell you that i have no idea
 * why `(ws.ws_row - 1) * 2 - 1` works.
 */
void cursor_page_up(void)
{
	int i = 0;

	for (i = 0; i < (ws.ws_row - 1) * 2 - 1; ++i)
		cursor_up();
}

void cursor_page_down(void)
{
	int i = 0;

	for (i = 0; i < (ws.ws_row - 1) * 2 - 1; ++i)
		cursor_down();
}

/*
 * since `lines` is a gap buffer of *indices*, we must remember to shift them
 * forwards and backwards for each insertion and deletion respectively.
 *
 * you have no idea how long i spent on this mistake.
 */
void decrement_lines(void)
{
	int i = 0;

	for (i = line + 1; i < lines_pos; ++i)
		--lines[i];

	for (i += lines_gap; i < lines_size; ++i)
		--lines[i];
}

/*
 * the delete operation in a gap buffer is rather routine
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * you probably guessed that you just need to increment the gap:
 *
 * c a _ _ _ _ _ a
 *     ^ pos = 2
 *     ^ ^ ^ ^ ^ gap = 5
 *
 * some extras:
 * 1. we cant do delete at the end of the file because the gap would go beyond
 *    the file size
 * 2. deleting at a newline character means we have to delete index for the
 *    next line
 */
void delete(void)
{
	int cursor = lines_get(line) + col;

	if (cursor + buf_gap >= buf_size)
		return;

	written = 0;

	if (buf_get_char(cursor) == '\n') {
		lines_move(line + 1);
		lines_gap++;
	}

	buf_move(cursor);
	buf_gap++;

	decrement_lines();
}

void delete_to_end_of_line(void)
{
	int i = 0;
	int n = lines_get_len(line) - col;

	for (i = 0; i < n; ++i)
		delete();
}

/*
 * backspace in a gap buffer is rather routine
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * c _ _ _ _ a c a
 *   ^ pos = 2
 *   ^ ^ ^ ^ gap = 4
 *
 * c _ _ _ _ _ c a
 *   ^ pos = 2
 *   ^ ^ ^ ^ gap = 5
 *
 * some extras:
 * 1. cant backspace at the beginning of the file
 * 2. backspacing at the beginning of a line means you need to delete the index
 *    for the current one.
 */
void backspace(void)
{
	int cursor = lines_get(line) + col;

	if (line == 0 && col == 0)
		return;

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

/*
 * saving is rather satisfying with gap buffers. just write the chunk before
 * and after the gap and call it a day :)
 */
void save(void)
{
	FILE *f;

	f = fopen(filename, "w");

	fwrite(buf, buf_pos, 1, f);
	fwrite(buf + buf_pos + buf_gap, buf_size - buf_pos - buf_gap, 1, f);

	fclose(f);

	written = 1;
	quit_confirm = 0;
	status = WRITTEN;
}

void quit(void)
{
	if (!written && !quit_confirm) {
		quit_confirm = 1;
		status = QUIT_CONFIRM;
	} else {
		exit(EXIT_SUCCESS);
	}
}

void exiting(void)
{
	alt_buf_off();
	raw_mode_off();
	free(lines);
	free(view);
	free(buf);
}

void insert_char(char c)
{
	int i = '\0';

	written = 0;

	buf_move(lines_get(line) + col);
	buf_insert_char(c);
	++col;

	/*
	 * since `lines` is a gap buffer of *indices*, we must remember to shift
	 * them forwards and backwards for each insertion and deletion
	 * respectively.
	 *
	 * you have no idea how long i spent on this mistake.
	 */
	for (i = line + 1; i < lines_pos; ++i)
		++lines[i];

	for (i += lines_gap; i < lines_size; ++i)
		++lines[i];
}

void insert_line(void)
{
	insert_char('\n');
	lines_move(line + 1);
	lines_insert(lines_get(line) + col);
	cursor_down();
	cursor_home();
}

/*
 * this function is half baked LOL... its taken from the
 * (`editorReadKey`)[https://github.com/antirez/kilo/blob/master/kilo.c#L253]
 * function in the `kilo` text editor, but i found it hard to read so i tried
 * to move some things around. this is rather rudimentary and doesnt handle
 * special keyw with modifiers. if you want a fully featured example you can
 * check out the
 * (`parse_event`)[https://gitlab.redox-os.org/redox-os/termion/-/blob/master/src/event.rs?ref_type=heads#L145]
 * function in the `termion` rust crate. its not my style but i appreciate its
 * rigor.
 *
 * as for the technical details themselves:
 *
 * 1. if you dont know how the escape sequences are formatted, this function is
 *    rather confusing. you can use the `read` command in any posix system and
 *    press the arrow keys, del key, home, end, pg up and down, etc. to see
 *    what gets sent to stdin (also with modifiers if you please).
 * 2. since our "raw mode" configuration makes us read only one byte at a time,
 *    this makes reading the multiple character escape sequences a little
 *    difficult. since we dont know how long the escape sequence is in advance
 *    we have to read the input in a loop.
 */
int input_process(void)
{
	char c = '\0';
	char seq[3] = { 0 };

	while (read(STDIN_FILENO, &c, 1) == 0);

	if (c != ESC)
		return c;

	/*
	 * put this in a while loop because we dont know how many characters
	 * can be in an escape sequence
	 */
	while (1) {
		/* just incase this simply is an ESC input */
		if (read(STDIN_FILENO, &seq[0], 1) == 0)
			return ESC;
		else if (read(STDIN_FILENO, &seq[1], 1) == 0)
			return ESC;

		if (seq[0] != '[')
			continue;

		/* extended escape sequence! */
		if (seq[1] >= '0' && seq[1] <= '9') {
			if (read(STDIN_FILENO, &seq[2], 1) == 0)
				return ESC;

			if (seq[2] != '~')
				continue;

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

/*
 * nothing special here, just a long chain of switch statements. if you would
 * like, you could change some of the keybinds to ones you prefer!
 *
 * if you want to know how the `CTRL_KEY` macro works, you can go to the top of
 * the file for its macro definition.
 */
void input_normal(int c)
{
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

/*
 * nothing special here, just a long chain of switch statements. if you would
 * like, you could change some of the keybinds to ones you prefer!
 *
 * so funnily enough we dont use a gap buffer for the search query since its
 * a string of a small fixed size. i didnt want to stitch the query together
 * for when we call `memmem` so i just decided to keep it an old fashioned
 * array.
 */
void input_search(int c)
{
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

/*
 * although this text editor wasnt meant to be modal, the search feature ended
 * up requiring its own spot in the ui so this reflects in the input system.
 * its nothing fancy though, just a simple if statement. most of the *real*
 * work is done in `input_process`
 */
void input(void) {
	int c = input_process();

	if (searching) {
		input_search(c);
	} else {
		input_normal(c);
	}
}

/*
 * welcome! this is a pretty rudimentary initialization -> lifecycle scheme.
 *
 * we dont return EXIT_SUCCESS because we would never actually reach that. we
 * use `exit(EXIT_SUCCESS)` in the `quit` function and use `atexit` to clean.
 *
 * nothing much to note from there. jump to any function youre curious about!
 * i suggest starting with `input`.
 */
int main(int argc, char **argv)
{
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
}
