# HTML named character references

`entities.js` is the HTML Standard's table of named character references
(`&amp;`, `&eacute;`, ... 2,231 names), as published in web-platform-tests
(`html/syntax/parsing/named-character-references-data.js`, commit
`64f13635ce4a216afe11c471db29f5692359eebc`). The data comes from the HTML
Standard; the file is under web-platform-tests' 3-Clause BSD licence (see
`LICENSE.md`). It matches Python's `html.entities.html5` exactly.

The HTML Standard says this list "is static and will not be expanded or
changed in the future". Unmodified. At build time `tools/gen_entities.py`
compiles it into `tc_entities.c`.
