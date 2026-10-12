/*
 * A page's lines into blocks, ready to render: paragraphs and headings with
 * their inline Markdown, code and tables as they are, list items carrying
 * their bullet and the column their text hangs at, notes and quotes
 * flagged. Containers -- list items, notes, quotes -- are parsed by parsing
 * their lines again, dedented, a level in.
 *
 * The Markdown read: ATX and setext headings ({#id} allowed), paragraphs
 * ({#id} at the end allowed), fenced and indented code, tables (with
 * {#id} in cells), lists nested by two spaces or more, block quotes, notes
 * (`!!! kind "title"`, and `???`), footnote definitions, rules; front
 * matter and [TOC] are skipped.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"

static page* pg;            /* the page being parsed */
static block* shared;       /* its blocks, the same room for every page */
static int max_blocks;

/* --- lines --------------------------------------------------------------------- */

/* The spaces a line starts with, counted: as input.c counts them, for a
 * line made here from part of another. */
static int count_indent(const char* s, int len)
{
    int k = 0;

    while (k < len && s[k] == ' ') {
        k++;
    }

    return k;
}

/* Found once, as the line was read (input.c), or made (dedent). */
static int indent_of(const line* l)
{
    return l->indent;
}

static int blank(const line* l)
{
    return l->indent == l->len;
}

/* l's text from its k-th column on, as a new line: its indent what was
 * left of it, unless k cut into its text. */
static line dedent(const line* l, int k)
{
    line d = *l;

    if (k > l->len) {
        k = l->len;
    }
    d.text += k;
    d.len -= k;
    d.indent = k <= l->indent ? l->indent - k : count_indent(d.text, d.len);

    return d;
}

/* The first line from i that is not blank, or n. */
static int next_text(const line* L, int n, int i)
{
    while (i < n && blank(&L[i])) {
        i++;
    }

    return i;
}

/* pat in the n bytes at s, or NULL: a line is not NUL-terminated, as it
 * lies in page_next's window. */
static const char* find(const char* s, int n, const char* pat)
{
    int m = (int)strlen(pat);
    int k;

    for (k = 0; k + m <= n; k++) {
        if (memcmp(s + k, pat, (size_t)m) == 0) {
            return s + k;
        }
    }

    return NULL;
}

static int starts_at(const char* text, int len, const char* s)
{
    int n = (int)strlen(s);

    return len >= n && memcmp(text, s, (size_t)n) == 0;
}

static int starts(const line* l, const char* s)
{
    return starts_at(l->text, l->len, s);
}

/* --- what a line starts ------------------------------------------------------ */

/* A fence: three or more ` or ~, at an indent under four. Its length. */
static int fence(const line* l, char* mark)
{
    int i = indent_of(l);
    int k = i;

    if (i >= 4 || i >= l->len || (l->text[i] != '`' && l->text[i] != '~')) {
        return 0;
    }
    *mark = l->text[i];
    while (k < l->len && l->text[k] == *mark) {
        k++;
    }
    if (k - i < 3) {
        return 0;
    }
    /* A backtick fence's info string has no backtick: ```code``` on one
     * line is a code span. */
    if (*mark == '`' && memchr(l->text + k, '`', (size_t)(l->len - k))) {
        return 0;
    }

    return k - i;
}

/* An ATX heading: its level, or 0. */
static int atx(const line* l)
{
    int i = indent_of(l);
    int k = i;

    if (i >= 4) {
        return 0;
    }
    while (k < l->len && l->text[k] == '#') {
        k++;
    }
    if (k == i || k - i > 6 || (k < l->len && l->text[k] != ' ')) {
        return 0;
    }

    return k - i;
}

/* A setext underline: 1 for ===, 2 for ---, else 0. */
static int setext(const line* l)
{
    int i = indent_of(l);
    int k;
    char c;

    if (i >= 4 || i >= l->len || (l->text[i] != '=' && l->text[i] != '-')) {
        return 0;
    }
    c = l->text[i];
    for (k = i; k < l->len && l->text[k] == c; k++) {
    }
    while (k < l->len && l->text[k] == ' ') {
        k++;
    }

    return k == l->len ? (c == '=' ? 1 : 2) : 0;
}

/* A rule: three or more of one of - * _, perhaps spaced. */
static int rule(const line* l)
{
    int i = indent_of(l);
    int n = 0;
    int k;
    char c;

    if (i >= 4 || i >= l->len) {
        return 0;
    }
    c = l->text[i];
    if (c != '-' && c != '*' && c != '_') {
        return 0;
    }
    for (k = i; k < l->len; k++) {
        if (l->text[k] == c) {
            n++;
        } else if (l->text[k] != ' ') {
            return 0;
        }
    }

    return n >= 3;
}

static int quote(const line* l)
{
    int i = indent_of(l);

    return i < 4 && i < l->len && l->text[i] == '>';
}

static int note(const line* l)
{
    int i = indent_of(l);

    return i < 4 && (starts_at(l->text + i, l->len - i, "!!! ")
                     || starts_at(l->text + i, l->len - i, "??? ")
                     || starts_at(l->text + i, l->len - i, "???+ "));
}

/* A list item: its bullet's length (with what follows it up to the text),
 * its indent, and whether it is numbered; 0 if it is not one. */
static int item(const line* l, int* ind, int* numbered, int* text_at)
{
    int i = indent_of(l);
    int k = i;

    if (k >= l->len) {
        return 0;
    }
    if (l->text[k] == '-' || l->text[k] == '*' || l->text[k] == '+') {
        k++;
        *numbered = 0;
    } else {
        while (k < l->len && l->text[k] >= '0' && l->text[k] <= '9' && k - i < 9) {
            k++;
        }
        if (k == i || k >= l->len || (l->text[k] != '.' && l->text[k] != ')')) {
            return 0;
        }
        k++;
        *numbered = 1;
    }
    if (k < l->len && l->text[k] != ' ') {
        return 0;
    }
    *ind = i;
    *text_at = k + 1 < l->len ? k + 1 : l->len;
    while (*text_at < l->len && *text_at - k < 5 && l->text[*text_at] == ' ') {
        (*text_at)++;
    }

    return k - i;
}

static int footnote(const line* l)
{
    return indent_of(l) == 0 && starts(l, "[^") && memchr(l->text, ']', (size_t)l->len)
           && find(l->text, l->len, "]:") != NULL;
}

/* --- tables ---------------------------------------------------------------------- */

int split_row(const char* s, int len, const char** cells, int* lens, int max)
{
    int n = 0;
    int k = 0;
    int start;
    int in_code = 0;

    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    while (k < len && s[k] == ' ') {
        k++;
    }
    if (k < len && s[k] == '|') {
        k++;
    }
    if (len > k && s[len - 1] == '|' && (len < 2 || s[len - 2] != '\\')) {
        len--;
    }
    start = k;
    for (; k <= len; k++) {
        if (k < len && s[k] == '`') {
            int run = 0;
            int j;

            while (k + run < len && s[k + run] == '`') {
                run++;
            }
            if (!in_code) {
                /* Code only when the run is closed later in the row. */
                for (j = k + run; j + run <= len; j++) {
                    if (memcmp(s + j, s + k, (size_t)run) == 0
                        && (j + run == len || s[j + run] != '`') && s[j - 1] != '`') {
                        in_code = run;
                        break;
                    }
                }
            } else if (in_code == run) {
                in_code = 0;
            }
            k += run - 1;
            continue;
        }
        if (k == len || (s[k] == '|' && !in_code && (k == 0 || s[k - 1] != '\\'))) {
            int a = start;
            int b = k;

            while (a < b && s[a] == ' ') {
                a++;
            }
            while (b > a && s[b - 1] == ' ') {
                b--;
            }
            if (n < max) {
                cells[n] = s + a;
                lens[n] = b - a;
            }
            n++;
            start = k + 1;
        }
    }

    return n < max ? n : max;
}

static int separator_row(const line* l)
{
    int k;
    int dash = 0;

    for (k = 0; k < l->len; k++) {
        char c = l->text[k];

        if (c == '-') {
            dash = 1;
        } else if (c != '|' && c != ':' && c != ' ') {
            return 0;
        }
    }

    return dash;
}

static int table_start(const line* L, int n, int i)
{
    static const char* cells[64];
    static int lens[64];
    int a;
    int b;

    if (i + 1 >= n || !memchr(L[i].text, '|', (size_t)L[i].len) || !separator_row(&L[i + 1])
        || !memchr(L[i + 1].text, '|', (size_t)L[i + 1].len)) {
        return 0;
    }
    a = split_row(L[i].text, L[i].len, cells, lens, 64);
    b = split_row(L[i + 1].text, L[i + 1].len, cells, lens, 64);

    return a == b;
}

/* --- blocks --------------------------------------------------------------------- */

/* Room for one more block. Blocks are kept by number while a page is
 * parsed, never by pointer across a parse, so the array can move. */
static void make_room(void)
{
    if (pg->nblocks == max_blocks) {
        max_blocks = max_blocks ? max_blocks * 2 : 64;
        shared = md_realloc(shared, (unsigned)max_blocks * (unsigned)sizeof(block));
        pg->blocks = shared;
    }
}

static block* add(int kind, int no)
{
    block* b;

    make_room();
    b = &pg->blocks[pg->nblocks++];
    memset(b, 0, sizeof *b);

    b->kind = (uint8_t)kind;
    b->no = no;

    return b;
}

/* Lines a..b-1 joined with '\n', each trimmed; a line that ended in two
 * spaces gets a <br>. */
static char* join(const line* L, int a, int b)
{
    unsigned size = 1;
    char* out;
    char* o;
    int k;

    for (k = a; k < b; k++) {
        size += (unsigned)L[k].len + 6;
    }
    o = out = source_alloc(size);
    for (k = a; k < b; k++) {
        line t = dedent(&L[k], indent_of(&L[k]));
        int len = t.len;
        int br = k + 1 < b && len >= 2 && t.text[len - 1] == ' ' && t.text[len - 2] == ' ';

        while (len > 0 && t.text[len - 1] == ' ') {
            len--;
        }
        memcpy(o, t.text, (size_t)len);
        o += len;
        if (br) {
            memcpy(o, "<br>", 4);
            o += 4;
        }
        if (k + 1 < b) {
            *o++ = '\n';
        }
    }
    *o = 0;

    return out;
}

/* A trailing {#id ...} cut from s, into *ids; s's new length. */
static int cut_attrs(char* s, int len, const char*** ids, uint8_t* nids)
{
    int e = len;
    int a;
    int k;

    while (e > 0 && s[e - 1] == ' ') {
        e--;
    }
    if (e == 0 || s[e - 1] != '}') {
        return len;
    }
    for (a = e - 1; a > 0 && s[a] != '{'; a--) {
    }
    if (s[a] != '{' || !memchr(s + a, '#', (size_t)(e - a))) {
        return len;
    }
    for (k = a + 1; k < e - 1; k++) {
        if (s[k] == '#' && (s[k - 1] == ' ' || s[k - 1] == '{' || s[k - 1] == ':')) {
            int j = k + 1;
            const char** more;

            while (j < e - 1 && s[j] != ' ' && s[j] != '}') {
                j++;
            }
            more = source_alloc((unsigned)(*nids + 1) * sizeof(char*));
            if (*nids) {
                memcpy(more, *ids, *nids * sizeof(char*));
            }
            more[(*nids)++] = source_strndup(s + k + 1, (unsigned)(j - k - 1));
            *ids = more;
        }
    }
    while (a > 0 && s[a - 1] == ' ') {
        a--;
    }
    s[a] = 0;

    return a;
}

static void heading(int level, const char* s, int len, int no)
{
    char* t = source_strndup(s, (unsigned)len);
    const char** ids = NULL;
    uint8_t nids = 0;
    int n = len;
    block* b;

    while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '#')) {
        if (t[n - 1] == '#' && n > 1 && t[n - 2] != ' ' && t[n - 2] != '#') {
            break;                          /* a # inside the text */
        }
        n--;
    }
    t[n] = 0;
    n = cut_attrs(t, n, &ids, &nids);
    b = add(B_HEAD, no);
    b->level = (uint8_t)(level > 4 ? 4 : level);
    b->text = t;
    b->id = nids ? ids[0] : NULL;
}

static int starts_block(const line* L, int n, int i)
{
    int ind;
    int num;
    int at;
    char mark;

    if (blank(&L[i])) {
        return 1;
    }
    if (i + 1 < n && setext(&L[i + 1]) && indent_of(&L[i]) < 4) {
        return 1;
    }

    return fence(&L[i], &mark) || atx(&L[i]) || note(&L[i]) || quote(&L[i])
           || (item(&L[i], &ind, &num, &at) && at < L[i].len) || table_start(L, n, i);
}

static void parse(const line* L, int n, int indent, int in_note, int first_space);

/* The lines indented by width or more after line i (or blank between),
 * dedented: how many, into a new array at *out. */
static int indented_body(const line* L, int n, int i, int width, line** out)
{
    int e = i;
    int k;

    while (e < n) {
        if (blank(&L[e])) {
            int j = next_text(L, n, e);

            if (j < n && indent_of(&L[j]) >= width) {
                e = j;
                continue;
            }
            break;
        }
        if (indent_of(&L[e]) < width) {
            break;
        }
        e++;
    }
    *out = source_alloc((unsigned)(e - i + 1) * sizeof(line));
    for (k = i; k < e; k++) {
        (*out)[k - i] = dedent(&L[k], width);
    }

    return e - i;
}

/* A list from line i: its items at one indent (or one more), each parsed
 * from its own lines. Where the list ends. */
static NOINLINE int list(const line* L, int n, int i, int indent, int in_note, int space)
{
    int base;
    int numbered;
    int at;
    int number = 1;
    int first = 1;
    int loose = 0;
    int ind;
    int num;

    item(&L[i], &base, &numbered, &at);
    if (numbered) {
        number = 0;
        for (at = base; at < L[i].len && L[i].text[at] >= '0' && L[i].text[at] <= '9'; at++) {
            number = number * 10 + L[i].text[at] - '0';
        }
    }
    while (i < n && item(&L[i], &ind, &num, &at) && ind >= base && ind < base + 2
           && num == numbered) {
        int content = at;
        int e = i + 1;
        int lines = 0;
        int had_blank = 0;
        line* body;
        int k;
        char marker[16];
        int before = pg->nblocks;
        block* b;

        while (e < n) {
            if (blank(&L[e])) {
                int j = next_text(L, n, e);

                if (j < n && indent_of(&L[j]) > base
                    && !(item(&L[j], &ind, &num, &at) && ind < base + 2)) {
                    had_blank = 1;
                    e = j;
                    continue;
                }
                break;
            }
            if (item(&L[e], &ind, &num, &at) && ind < base + 2) {
                break;                      /* the next item, or a list after */
            }
            if (indent_of(&L[e]) > base) {
                e++;
                continue;
            }
            if (!starts_block(L, n, e) && !blank(&L[e - 1])) {
                e++;                        /* a lazy continuation */
                continue;
            }
            break;
        }
        body = source_alloc((unsigned)(e - i + 1) * sizeof(line));
        body[lines++] = dedent(&L[i], content);
        for (k = i + 1; k < e; k++) {
            int d = indent_of(&L[k]);

            body[lines++] = dedent(&L[k], d > base ? (d < content ? d : content) : d);
        }
        if (numbered) {
            sprintf(marker, "%d. ", number++);
        } else {
            strcpy(marker, "- ");
        }
        loose |= had_blank;
        parse(body, lines, indent + (int)strlen(marker), in_note, first ? space : loose);
        if (pg->nblocks > before && pg->blocks[before].kind == B_PARA
            && !pg->blocks[before].marker) {
            b = &pg->blocks[before];
        } else {
            /* The item opens with something else: the bullet on its own. */
            make_room();
            memmove(&pg->blocks[before + 1], &pg->blocks[before],
                    (size_t)(pg->nblocks - before) * sizeof(block));
            pg->nblocks++;
            b = &pg->blocks[before];
            memset(b, 0, sizeof *b);
            b->kind = B_PARA;
            b->no = L[i].no;
            b->text = "";
            b->space = (uint8_t)(first ? space : loose);
        }
        b->marker = source_strndup(marker, (unsigned)strlen(marker));
        b->indent = (uint8_t)indent;
        first = 0;
        i = e;
        /* A blank line, then the next item at the same indent: one list. */
        k = next_text(L, n, i);
        if (k < n && k > i && item(&L[k], &ind, &num, &at) && ind == base && num == numbered) {
            loose = 1;
            i = k;
        }
    }

    return i;
}

/* Each block below starts at L[i] and returns the line after it. Each has
 * its own frame: inlined into parse, which recurses, their locals added up
 * past the 128 bytes the eZ80 reaches cheaply (test/frames.sh). */

/* ``` or ~~~ ... the same fence: the text kept as it is. -1 if never closed. */
static NOINLINE int fenced_code(const line* L, int n, int i, int len, char mark)
{
    int ind = indent_of(&L[i]);
    int e = i + 1;
    unsigned size = 1;
    char* code;
    char* o;
    int k;

    while (e < n) {
        char m2;
        int len2 = fence(&L[e], &m2);
        line rest = dedent(&L[e], indent_of(&L[e]) + len2);

        if (len2 >= len && m2 == mark && blank(&rest)) {
            break;
        }
        size += (unsigned)L[e].len + 1;
        e++;
    }
    if (e >= n) {
        md_error(pg->path, L[i].no, "code block never closed");

        return -1;
    }
    o = code = source_alloc(size);
    for (k = i + 1; k < e; k++) {
        int d = indent_of(&L[k]);
        line t = dedent(&L[k], d < ind ? d : ind);

        memcpy(o, t.text, (size_t)t.len);
        o += t.len;
        if (k + 1 < e) {
            *o++ = '\n';
        }
    }
    *o = 0;
    add(B_PRE, L[i].no)->text = code;

    return e + 1;
}

/* # Title */
static NOINLINE int atx_heading(const line* L, int i, int level)
{
    line t = dedent(&L[i], indent_of(&L[i]) + level);
    line s = dedent(&t, indent_of(&t));

    heading(level, s.text, s.len, L[i].no);

    return i + 1;
}

/* Title, then === or --- */
static NOINLINE int setext_heading(const line* L, int i)
{
    line s = dedent(&L[i], indent_of(&L[i]));

    heading(setext(&L[i + 1]), s.text, s.len, L[i].no);

    return i + 2;
}

/* !!! kind "title": the title emphasised, then its body, indented. */
static NOINLINE int note_block(const line* L, int n, int i, int indent, int space)
{
    const line* l = &L[i];
    int ind = indent_of(l);
    const char* s = l->text + ind;
    const char* q1 = memchr(s, '"', (size_t)(l->len - ind));
    const char* kind = s + (s[3] == '+' ? 5 : 4);
    block* b = add(B_PARA, l->no);
    line* body;
    int k;

    b->em = 1;
    b->note = 1;
    b->indent = b->hang = (uint8_t)(indent + 2);
    b->space = (uint8_t)space;
    if (q1) {
        const char* q2 = memchr(q1 + 1, '"', (size_t)(l->text + l->len - q1 - 1));

        b->text = source_strndup(q1 + 1, (unsigned)((q2 ? q2 : l->text + l->len) - q1 - 1));
    } else {
        char* t;
        int e = 0;

        while (kind + e < l->text + l->len && kind[e] != ' ') {
            e++;
        }
        t = source_strndup(kind, (unsigned)e);
        if (t[0] >= 'a' && t[0] <= 'z') {
            t[0] = (char)(t[0] - 32);
        }
        b->text = t;
    }
    k = indented_body(L, n, i + 1, 4, &body);
    parse(body, k, indent + 2, 1, 0);
    i += 1 + k;
    while (i < n && blank(&L[i])) {
        i++;
    }

    return i;
}

/* [^1]: text -- shown where it is, as [1] text. */
static NOINLINE int footnote_block(const line* L, int n, int i)
{
    const line* l = &L[i];
    const char* close = memchr(l->text, ']', (size_t)l->len);
    int label = (int)(close - l->text) - 2;
    int e = i + 1;
    char* t;
    char* c;
    char* out;

    while (e < n && !blank(&L[e]) && indent_of(&L[e]) >= 4) {
        e++;
    }
    t = join(L, i, e);
    c = strstr(t, "]:");
    out = source_alloc((unsigned)strlen(t) + 4);
    sprintf(out, "[%.*s] %s", label, l->text + 2, c ? c + 2 + (c[2] == ' ') : "");
    add(B_PARA, l->no)->text = out;

    return e;
}

/* > lines: their text, without the >, parsed as a container. */
static NOINLINE int quote_block(const line* L, int n, int i, int indent, int space)
{
    int e = i;
    line* body;
    int k;

    while (e < n && !blank(&L[e]) && (quote(&L[e]) || !starts_block(L, n, e))) {
        e++;
    }
    body = source_alloc((unsigned)(e - i + 1) * sizeof(line));
    for (k = i; k < e; k++) {
        line t = L[k];

        if (quote(&t)) {
            t = dedent(&t, indent_of(&t) + 1);
            if (t.len && t.text[0] == ' ') {
                t = dedent(&t, 1);
            }
        }
        body[k - i] = t;
    }
    parse(body, e - i, indent + 2, 1, space);

    return e;
}

/* | rows |, the separator row dropped; anchors set in cells, {#id}, go with
 * the table. */
static NOINLINE int table_block(const line* L, int n, int i)
{
    int e = i + 2;
    unsigned size = (unsigned)L[i].len + 2;
    block* b;
    char* t;
    char* o;
    int k;

    while (e < n && !blank(&L[e]) && memchr(L[e].text, '|', (size_t)L[e].len)) {
        size += (unsigned)L[e].len + 1;
        e++;
    }
    o = t = source_alloc(size);
    for (k = i; k < e; k++) {
        if (k == i + 1) {
            continue;
        }
        memcpy(o, L[k].text, (size_t)L[k].len);
        o += L[k].len;
        *o++ = '\n';
    }
    *o = 0;
    b = add(B_TABLE, L[i].no);
    b->text = t;
    for (k = i + 2; k < e; k++) {
        const char* s = L[k].text;
        const char* c;

        for (c = s; (c = find(c, (int)(s + L[k].len - c), "{#")) != NULL; c++) {
            const char* close = find(c, (int)(s + L[k].len - c), "}");
            char* idt = source_strndup(c, (unsigned)(close ? close - c + 1 : s + L[k].len - c));

            cut_attrs(idt, (int)strlen(idt), &b->ids, &b->nids);
        }
    }

    return e;
}

/* Indented code: blank lines inside it kept. */
static NOINLINE int indented_code(const line* L, int n, int i)
{
    line* body;
    int k = indented_body(L, n, i, 4, &body);
    unsigned size = 1;
    char* code;
    char* o;
    int j;

    for (j = 0; j < k; j++) {
        size += (unsigned)body[j].len + 1;
    }
    o = code = source_alloc(size);
    for (j = 0; j < k; j++) {
        memcpy(o, body[j].text, (size_t)body[j].len);
        o += body[j].len;
        if (j + 1 < k) {
            *o++ = '\n';
        }
    }
    *o = 0;
    add(B_PRE, L[i].no)->text = code;

    return i + k;
}

/* Lines up to the next block, joined; a trailing {#id} cut off. */
static NOINLINE int paragraph(const line* L, int n, int i)
{
    int e = i + 1;
    block* b;
    char* t;

    while (e < n && !starts_block(L, n, e)) {
        e++;
    }
    t = join(L, i, e);
    b = add(B_PARA, L[i].no);
    cut_attrs(t, (int)strlen(t), &b->ids, &b->nids);
    b->text = t;

    return e;
}

static void parse(const line* L, int n, int indent, int in_note, int first_space)
{
    int i = 0;
    int space = first_space;
    int count = 0;              /* blocks so far in this container */

    while (i < n) {
        const line* l = &L[i];
        int ind = indent_of(l);
        int level;
        int len;
        int num;
        int at;
        char mark;
        block* b;

        if (blank(l)) {
            i++;
            continue;
        }
        space = count > 0 || first_space;
        if (ind < 4 && starts_at(l->text + ind, l->len - ind, "[TOC]")) {
            i++;
            continue;
        }

        /* In the order they are tried. A container (a note, a quote or a
         * list) sets its own blocks, and goes round again. */
        if ((len = fence(l, &mark)) != 0) {
            i = fenced_code(L, n, i, len, mark);
            if (i < 0) {
                return;
            }
        } else if ((level = atx(l)) != 0) {
            i = atx_heading(L, i, level);
        } else if (note(l)) {
            i = note_block(L, n, i, indent, space);
            count++;
            continue;
        } else if (i + 1 < n && ind < 4 && setext(&L[i + 1]) && !quote(l)
                   && !item(l, &level, &num, &at) && !memchr(l->text, '|', (size_t)l->len)) {
            i = setext_heading(L, i);
        } else if (footnote(l)) {
            i = footnote_block(L, n, i);
        } else if (rule(l)) {
            add(B_RULE, l->no);
            i++;
        } else if (quote(l)) {
            i = quote_block(L, n, i, indent, space);
            count++;
            continue;
        } else if (table_start(L, n, i)) {
            i = table_block(L, n, i);
        } else if (item(l, &level, &num, &at)) {
            i = list(L, n, i, indent, in_note, space && (count > 0 || first_space)
                     && (i == 0 || blank(&L[i - 1]) || count == 0 ? 1 : 0));
            count++;
            continue;
        } else if (ind >= 4) {
            i = indented_code(L, n, i);
        } else {
            i = paragraph(L, n, i);
        }
        b = &pg->blocks[pg->nblocks - 1];
        if (b->kind != B_HEAD) {
            b->indent = (uint8_t)indent;
            b->hang = (uint8_t)indent;
        }
        b->note = (uint8_t)in_note;
        b->space = (uint8_t)space;
        count++;
    }
}

/* --- for page_next: what a line is ------------------------------------------- */

int line_blank(const line* l)
{
    return blank(l);
}

int line_indent(const line* l)
{
    return indent_of(l);
}

int line_item(const line* l)
{
    int ind;
    int numbered;
    int at;

    return item(l, &ind, &numbered, &at) != 0;
}

int line_fence(const line* l, char* mark)
{
    return fence(l, mark);
}

/* Whether l closes a fence of len marks: as fenced_code closes one. */
int line_closes(const line* l, int len, char mark)
{
    char m2;
    int len2 = fence(l, &m2);
    line rest = dedent(l, indent_of(l) + len2);

    return len2 >= len && m2 == mark && blank(&rest);
}

/* --- a page ------------------------------------------------------------------- */

void parse_begin(page* p)
{
    pg = p;
    p->blocks = shared;
    p->nblocks = 0;
}

void parse_run(page* p, const line* L, int n)
{
    pg = p;
    parse(L, n, 0, 0, 1);
}
