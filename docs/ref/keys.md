# The keys

`?` or F1 shows them in ref. How ref is started is in [Using ref](using.md).

## In a topic

| Key | Does |
|---|---|
| arrows | move the cursor; the screen follows it |
| PgUp, PgDn, Home, End | a page up or down, or to either end (Space and `b` page too) |
| Ctrl+Up, Ctrl+Down | the cursor to the previous or next heading |
| Ctrl+Left, Ctrl+Right | the cursor to the previous or next link (Shift+Tab and Tab too) |
| Enter | follow the link under the cursor |
| Backspace, Alt+Left | back: to the link you followed, or to the contents, list or search you came from |
| Alt+Right | forward |
| `n`, `p` | the next or previous topic |
| `u` | the section the topic is in |
| `t` | the contents |
| `/`, F3 | search |
| `?`, F1 | the keys |
| Esc, `q` | quit |

## `Ctrl+F`: finding words

Ctrl+F asks for words on the bottom row and finds them, from the top of the
screen down, ignoring case; a search may be several words. Ctrl+N and
Ctrl+P go to the next match and the one before. Esc, or Enter with
nothing typed, gives up. The last words found are kept: in the next topic,
or in the contents, Ctrl+N finds them again.

## In the contents

`ref <doc>` opens a document's contents, shaped as its source is: its
sections, its pages and their headings, as a tree. The arrows move, Right
and Left open and close a section, and Enter opens a topic. Ctrl+F finds
words in the titles, in sections that are shut too, and opens the section
a match is in; Ctrl+N and Ctrl+P go to the next and the one before. `t`
or Esc goes back to the topic the contents were opened from, or, with
none, to the documents.

## `/`: search as you type

`/` or F3 opens a list that each key typed narrows: the keys that start
with what is typed first, then the topics the search finds. Enter opens one;
Backspace from it comes back to the list as it was.
