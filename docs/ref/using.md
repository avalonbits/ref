# Using ref

ref reads documentation on the Agon: the community documentation, `mos`,
the whole wiki in one file, and any program's own. It follows a
document's links and searches it. ref runs on the Agon Light, Light 2,
Origins and Console8, under MOS 3.0.2. Its keys are in [The keys](keys.md),
its options one by one in [ref's options](options.md).

## Installing

A ref release unzips onto the root of the SD card:

    /bin/ref.bin                    the reader
    /bin/md2ref.bin                 to write documents
    /config/ref/docs/mos.dat        agon-docs
    /config/ref/docs/ref.dat        this book
    /config/ref/docs/<name>.dat     any other document

MOS finds `ref` in `/bin` from any directory.

## Documents

A document is one file, `<name>.dat`, and its name is the file's without
`.dat`: one to eight lower-case letters and digits. ref looks for it in
the current directory first, where a program can keep its own, and then
in `/config/ref/docs`. Plain `ref` lists both, the current directory's
marked `.`, to choose one: Enter opens it, Esc quits.

    */ ref
    */ ref mos
    */ ref ref

## Looking things up

    ref <doc> <query>       look it up; search the text if nothing matches
    ref <doc> -k <query>    look it up, and nothing else
    ref <doc> -s <words>    search the text

A lookup asks the document's keys: the code spans in its headings, the VDU
sequences they name, and the words of their titles. Case does not matter.
A lookup that finds one topic opens it; more than one, a list to choose
from. Nothing found, ref searches the document's words instead: every
word, in `mos`; in a document md2ref made, the topics' names -- titles,
headings, and what is in a code span, a link or an emphasis -- unless it
was made with `-f`. The topics with the words in their titles come first.
What `mos` knows is in [Looking things up in mos](mos.md).

## Several documents

    ref <a>,<b>,... <query>

The first of the documents with an answer: `ref myprog,mos -k strlen`
asks `myprog` and then `mos`. One that is missing is passed over.

## What ref returns

0 when it showed something and 100 when nothing matched, or there is no
such document, so a script can tell; never 1, 4 or 5, which MOS reports as
errors of its own. ade's F1 runs `ref mos -k <word>` through hub this way.

## The screen

The document's title is across the top, the text in the middle, and a
status line at the bottom says where the topic sits in the document and
how far down it you are. Links are green and headings yellow. A cursor
moves through the text, as in a text-mode web browser: the link or
heading under it is lit. ref uses the screen mode and font already in use;
under hub, the prompt's.
