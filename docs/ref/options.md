# ref's options

    ref [<doc>[,<doc>...] [-k | -s] [query]] [-p] [-v] [-f <file>]

Options can come anywhere. The first other word is the document, unless
`-f` names one, and the rest is the query. How they fit together is in
[Using ref](using.md).

## `-k`: keys only

    ref <doc> -k <query>

Look the query up in the keys, and do not search the text if nothing
matches.

## `-s`: search only

    ref <doc> -s <words>

Search the document's words for them, without looking them up first.

## `-p`: print

Print instead of showing: the topic found, or the list of what matched.
Alone, `ref -p` prints the documents, with their titles.

## `-f`: a document by its path

    ref -f <file> ...

A document by its path instead of its name. A `<doc>` with a `/` in it,
or ending in `.dat`, is a path too.

## `-v`: the version

    ref -v [doc]

ref's version, and, with a document, where it came from: mos.dat says
the agon-docs commit it was built from and its date.

## `-T`: timings

How long ref takes to open the document, look up, search, and read and
lay out its largest topic, on this machine, ten runs each.

## `-h`, `--help`: the usage

Print how ref is used, and stop.
