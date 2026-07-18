/*
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAP_BUF_GAP 8
#define GAP_BUF_INSERT_CONST_STR(g, s) gap_buf_insert(g, s, sizeof(s) - 1)

struct gap_buf {
	char *buf;
	int pos;
	int gap;
	int size;
};

void gap_buf_new(struct gap_buf *g, int s);
void gap_buf_grow(struct gap_buf *g, int n);
void gap_buf_insert(struct gap_buf *g, char *s, int n);
void gap_buf_insert_char(struct gap_buf *g, char c);
void gap_buf_move_left(struct gap_buf *g, int n);
void gap_buf_move_right(struct gap_buf *g, int n);
void gap_buf_move(struct gap_buf *g, int n);
void gap_buf_delete(struct gap_buf *g, int n);
void gap_buf_backspace(struct gap_buf *g, int n);
void gap_buf_print(struct gap_buf *g);

/*
 * initializes the gap buffer so that it can fit `s + GAP_BUF_GAP` elements.
 *
 * designed this way so that its straightforward to modify things like the
 * contents of a file.
 */
void gap_buf_new(struct gap_buf *g, int s)
{
	g->size = s + GAP_BUF_GAP;
	g->gap = GAP_BUF_GAP;
	g->buf = malloc(sizeof(*g->buf) * g->size);
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
void gap_buf_grow(struct gap_buf *g, int n)
{
	int bump = 0;
	int dst = 0;
	int src = 0;
	size_t size = 0;

	if (n <= g->gap)
		return;

	bump = (((n - g->gap) / GAP_BUF_GAP) + 1) * GAP_BUF_GAP;
	src = g->pos + g->gap;
	dst = src + bump;
	size = g->size - g->pos - g->gap;

	g->buf = realloc(g->buf, g->size + bump);
	memcpy(&g->buf[dst], &g->buf[src], size);
	g->size += bump;
	g->gap += bump;
}

/*
 * _ _ _ _ _ _ _ _
 * ^ pos = 0
 * ^ ^ ^ ^ ^ ^ ^ ^ gap = 8
 *
 * just copy the memory over
 *
 * c a c a _ _ _ _
 * ^ pos = 0
 * ^ ^ ^ ^ ^ ^ ^ ^ gap = 8
 *
 * then do some arithmetic
 *
 * c a c a _ _ _ _
 *         ^ pos = 4
 *         ^ ^ ^ ^ gap = 4
 */
void gap_buf_insert(struct gap_buf *g, char *s, int n)
{
	gap_buf_grow(g, n);
	memcpy(&g->buf[g->pos], s, n);
	g->pos += n;
	g->gap -= n;
}

/*
 * same as above
 */
void gap_buf_insert_char(struct gap_buf *g, char c)
{
	gap_buf_grow(g, 1);
	g->buf[g->pos++] = c;
	g->gap--;
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
void gap_buf_move_left(struct gap_buf *g, int n)
{
	if (n > g->pos)
		n = g->pos;

	memcpy(&g->buf[g->pos - n + g->gap], &g->buf[g->pos - n], n);
	g->pos -= n;
}

/*
 * essentiallly the same as above
 *
 * p.s. pos + gap isnt overcounting by 1 as pos is 0-indexed :D
 */
void gap_buf_move_right(struct gap_buf *g, int n)
{
	if (n > g->size - g->pos - g->gap)
		n = g->size - g->pos - g->gap;

	memcpy(&g->buf[g->pos], &g->buf[g->pos + g->gap], n);
	g->pos += n;
}

/*
 * moves point of insertion AFTER index `n`
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
void gap_buf_move(struct gap_buf *g, int n)
{
	if (g->pos - n > 0)
		gap_buf_move_left(g, g->pos - n);
	else if (g->pos - n < 0)
		gap_buf_move_right(g, n - g->pos);
}

/*
 * think of the DEL key on your keyboard
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * a deletion of 2 chars yields
 *
 * c a _ _ _ _ _ _
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 6
 */
void gap_buf_delete(struct gap_buf *g, int n)
{
	if (n > g->size - g->pos - g->gap)
		n = g->size - g->pos - g->gap;

	g->gap += n;
}

/*
 * think of the backspace key on your keyboard
 *
 * c a _ _ _ _ c a
 *     ^ pos = 2
 *     ^ ^ ^ ^ gap = 4
 *
 * backspacing 2 chars yields
 *
 * _ _ _ _ _ _ c a
 * ^ pos = 0
 * ^ ^ ^ ^ ^ ^ gap = 6
 */
void gap_buf_backspace(struct gap_buf *g, int n)
{
	if (n > g->pos)
		n = g->pos;

	g->pos -= n;
	g->gap += n;
}

/*
 * prints information in a similar format to this file's documentation :)
 */
void gap_buf_print(struct gap_buf *g)
{
	int i = 0;

	for (i = 0; i < g->pos; i++)
		printf("%c ", g->buf[i]);

	for (i = 0; i < g->gap; i++)
		printf("_ ");

	for (i = g->pos + g->gap; i < g->size; i++)
		printf("%c ", g->buf[i]);

	printf("\n");

	for (i = 0; i < g->pos * 2; i++)
		printf(" ");

	printf("^ pos = %d\n", g->pos);

	for (i = 0; i < g->pos * 2; i++)
		printf(" ");

	for (i = 0; i < g->gap; i++)
		printf("^ ");

	printf("gap = %d\n", g->gap);
	printf("size = %d\n\n", g->size);
}
