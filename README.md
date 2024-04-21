# XMLFS

## About
XMLFS is a FUSE filesystem that allows a user to mount an XML file and browse
through it's document tree as though the nodes were files and directories.

Obviously There is not a perfect mapping between the XML DOM and a filesystem
tree. This creates some interesting differences between the two and this
filesystem attempts to resolve some of the differences by using some language
borrowed from XPath.

### Multiple Same-name Nodes
XML allows an author to create multiple nodes with the same name. This creates
ambiguity when attempting to navigate the document. XPath already has a solution
in place: have some kind of specifier to make each item unique. We prefer the id
attribute, however, it can fall back to the class attribute, or simply the
number of the tag as it appears in the document. Currently only id and class
have been implemented. Numbers still aren't working yet.

### Text inside a directory
XML allows a node to have both text nodes inside it as well as other element
nodes. However, a directory in a POSIX filesystem cannot have data stored in it.
This creates an interesting problem of how to access that kind of data. I think
the answer here also borrows from XPath. XPath allows you to select text nodes
and these could be included in the directory as something like `text()[3]`. This
decision hasn't been made yet and is still open.

## Goals
1. Correctness
1. Reasonably quick parsing
1. Multi-threading capable
1. Cached lookups
1. Not insanely memory hungry

## Why?
This is mainly for fun. I like doing weird things with computers and parsing
formal languages has always been near the top of my fun list. However, there are
several practical uses for this filesystem:

### Shell Scripts
Working with XML in shell scripts can be a nightmare for multiple reasons.

#### Reparse
You can easily use another program to extract information from an XML file.
However, the basic workflow for that requires parsing the entire XML file for
every piece of information you want to extract. This can get time consuming if
the XML file is larger than a couple megabytes which isn't uncommon now days.

XMLFS allows you to keep the entire XML file in memory allowing you to quickly
find the information you want, and read it without a reparse required every time
you need a new piece of information.

### Existing Tools
I rarely use XML in my day-to-day work, but it does occasionally come up. When
that happens, I use to have to re-learn how to do XPath and many of the DOM
tools. This is time consuming when all I really need to do is find some tag that
matches a regex and extract some information from it. Now I can find it just
like I was looking for a file:

    find /mnt/xml -name 'author' -exec grep 'Author Douglas' '{}' ';'

## Dependencies
- [Quizno50XML](https://github.com/quizno50/Quizno50XML)
- [libfuse](https://github.com/libfuse/libfuse)

I used my own XML parser for this project. It's called Quizno50XML and it is
available on my GitHub. You will need to at least download it into the parent
directory if you want to build/use XMLFS.

I used libfuse version 3.16.2 while developing this. However, it will likely
work on older versions with some modifications.

## Compiling
I have included a Makefile which will likely need some modification for you to
build this project. However, if you have Quizno50XML in the parent directory
and libfuse is installed, you shouldn't have much trouble building the
filesystem.

## Running
Basic Usage:

    ./xmlfs --xml-file=test.xml /mnt/xml

If you would like to run in debug mode:

    ./xmlfs --xml-file=test.xml -f /mnt/xml

