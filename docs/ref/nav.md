# Sections, from `mkdocs.yml`

A directory with an `mkdocs.yml` beside its pages is read by its `nav`,
as MkDocs reads it, and as ref's builder makes mos.dat from agon-docs'.
Its sections become headings in the contents that open no topic, with
their pages under them.

## `nav`

    site_name: Agon C library
    nav:
      - Start: index.md
      - Input and output:
          - stdio.md
          - wchar.md
      - Strings:
          - string.md

- `- Title:` with items under it is a section: a heading in the contents,
  opening no topic.
- `- file.md`, or `- Title: file.md`, is a page, read in the nav's order.
  A title in the nav titles its line in the contents, and the page's topic
  too when the page has no `#` heading of its own. A title with a colon in
  it is quoted.
- The directory's pages the nav leaves out come last, under "Pages outside
  the menu".

## `site_name`

The document's title, unless `-t` gives one.

## Links

Links still name a page by its file, `stdio.md#fopen`, in any section.
