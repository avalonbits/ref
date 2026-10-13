# md2ref's options

    md2ref [-t <title>] [-w] [-z] [-f] [-e <file>] <name> [<dir> | <page.md>... | @list]
    md2ref [-t <title>] [-w] [-z] [-f] [-e <file>] -o <file.dat> <dir> | <page.md>... | @list

How they fit together is in [Using md2ref](md2ref.md).

## `-o`: the document's file

    -o <file.dat>

Write the document there instead of `ref-doc/<name>.dat`: to
`/config/ref/docs/myprog.dat`, say, to install it for everyone.

## `-t`: the title

    -t <title>

The document's title. Without it, an `mkdocs.yml`'s `site_name`, or else
the first page's first heading.

## `-w`: warnings

A link that leads nowhere is a warning, not an error.

## `-e`: errors to a file

    -e <file>

Errors and warnings go to the file as well as the console. The file is
made even when there are none, so a program that runs md2ref can read it
after.

## `-f`: every word

Put every word of every topic in the search's index. Without it, only the
names are: each topic's title and headings, and what is in a code span, a
link's label or an emphasis -- a tenth of the words, and of the time.

## `-z`: compressed

Compress each topic. The document is about 30% smaller, and md2ref takes
about two fifths longer on the Agon. Without it each topic is stored as it
is, which ref reads as it reads one compressed.

## `@list`: pages from a file

    md2ref <name> @pages.lst

The pages' paths, one a line, from a file: for more than a command line
holds.
