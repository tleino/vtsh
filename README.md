# vtsh

*vtsh* is a mashup of *a virtual terminal* (vt) and a command interpreter
a.k.a. *shell* (sh) for Unix-like operating systems such as Linux and
OpenBSD. It could also be thought *vtsh* means *vertical tabs of shells*.

*vtsh* is also a text editor, but *vtsh* is created foremost for
interacting with commands: the text editing facility come as a very
useful side effect. Nevertheless, *vtsh* can be used as a text editor
for all purposes.

In *vtsh*, the output of commands go to their own editable and viewable
buffers, the output can be used for launching new commands and the output
buffer can be used for interacting with the backend program in a
linear or non-linear fashion. The buffers can also be saved to file, or
the buffer contents can be redirected as input to other commands.

In other words, *vtsh* is like a command shell in Unix-like systems, but
*vtsh* is implemented for the screen-based devices instead of paper
teletype devices, this time using the advantages that screen-based devices
offer, unlike the traditional line-based shells.

## Screenshots

See the [screenshot](https://namhas.dev/vtsh.png) showing various
features.

See also the [animated screenshot](https://namhas.dev/vtsh-ed.gif)
showing *vtsh* running line-based editor from 1970s using *vtsh*'s
*slave buffers* and *buffer redirection* achieving cursor-addressable
editing without code modifications to the original *ed* editor. It means
even the venerable *ed* gains screen-based editing functionality when used
through *vtsh*, even though editing in *vtsh* does not require the use of
the *ed* editor.

## Original inspiration

*vtsh* is an exercise in taking the Unix philosophy to the extreme
in the footsteps of e.g. [acme](http://acme.cat-v.org/)
editor/user interface for
programmers that was originally created for the Plan 9 operating
system and its lesser known philosophical base described in
[A Minimal Global User Interface](http://doc.cat-v.org/plan_9/1st_edition/help/help.pdf)
paper written by Rob Pike.

The inspiration for exploring similar avenues came to *vtsh*'s author
out of frustration independently and unaware of Rob Pike's efforts
when the author was
* being helplessly undisciplined and always launching a new tab, or
a new window for every small task, only to lose the tab or the window
under the pile of hundred other tabs and windows, resulting in a need
to launch even more tabs or windows even for the same commands;
* being philosophically offended by the wastefulness of an endless
scrollback in a normal virtual terminal which keeps a backlog of very
redundant data that does not need to be saved, making the scrollback
mostly useless.

Originally the thought of implementing *vtsh* came to the author
after implementing a *paper teletype* device and figuring out how
would one use such a device efficiently. It would be stupid to waste
paper by re-running commands, and it would be difficult to find a
particular output from a long, endless scrollback. The solution was:
cutting useful outputs from the scrollback to small *slips of paper*
that would be laid out on a table so that all useful pieces would be
visible at the same time. The author wanted to do the same for a
screen-based device as a solution to the frustration with traditional
virtual terminals. This led to the development of
[cocowm](https://github.com/tleino/cocowm),
[cocovt](https://github.com/tleino/cocovt) and *vtsh*.

The principal idea in *vtsh* is that it keeps each command's output
in a separate buffer, alike to slips of paper, that can be independently
arranged and scrolled as wished.

Furthermore, *vtsh* enables potential for more simplicity and user
control for programmers and hackers-alike. When using *vtsh* as an
user interface, it becomes a layer that frees the *downstream programs
from having to reimplement cursor addressable user interfaces*, and
as it does that, the *user interface elements can be customized from
a single place*, thereby implementing some of the same ideas as
originally described by Rob Pike.

In practice, when using *vtsh*, it means
* standard I/O programs get editing, searching, windowing and paging
functionality for free, so that e.g. "the standard editor" ed(1),
which is originally a line-based editor, becomes an editor with full
screen editing capability without modifying a single line of code;
* thereby there is no longer need for having cursor addressable text user
interfaces in standard I/O programs but they can be kept plain and simple,
this means there is no longer need for ncurses(3), terminfo(5), and all that,
but downstream programs can be simple input and output programs that does
not have to *care about representation or controls*.

Not having to *care about representation or controls* means *simplicity*
and *ease of development*, as well as *composability of unrelated programs*,
but it also means more *user control* by providing a single layer which can
be used for customizing the representation and controls how ever the
user wishes.

## Sending input

In a perfect world all text-based software would be implemented in a
non-interactive stateless request-response form where one request
results in one response. That way we'd have the greatest simplicity and
composability, but in reality we have many types of text-based software:
* stateless request-response;
  * (e.g. *curl* requests to HTTP REST API)
* stateless request-response with a long-lived response stream;
  * (e.g. *iostat* configured to report status every second)
* stateful request-response stream (**use slave buffers**);
  * (e.g. *sh*, *telnet* or *ed* session)
* asynchronous request-response stream that may send response at any
random moment without a request (**use inline input**).
  * (e.g. *sh* with background commands, *mqtt* session or a chat session)
* request/response-input-response stream (**use buffer redirection**);
  * (e.g. *ed* session that displays output, which is then modified
and sent as standard input back as another request)

(**Help wanted**: give ideas how to describe these different types with
better wordings...)

In addition to these, we have cursor-addressable text-based software such
as *mutt*, *emacs* or *vi* but these will not be supported.

For supporting the different types of text-based software we need to both
support sending input in a traditional *typescript*-way, i.e. inline input to
the output stream (for asynchronous streams or for streams where the
visibility of past commands is important for getting a picture of the
current state) and for the other cases, we can use the command editor with
or without slave buffers. The slave buffers help when we have some state
but the state is not very interesting e.g. when we're running a long-lived
*ssh* session to a remote host.

### Slave buffers

Ctrl+S brings any number of slave buffers when an another slave buffer
or an active command is focused.

### Buffer redirection

When a command name ends with '<' the output buffer (a.k.a. typescript
buffer) is sent as standard input to the command regardless of whether
it was a slave buffer. The standard ^D (0x04) a.k.a. EOF is used as the
delimeter.

(TODO: Buffer redirection works at the moment only with slave buffers.)

If the command name ends with '<.' then the input is delimited with '.'
i.e. this is useful with e.g. 'ed'.

#### Examples

Redirect existing buffer contents to the 'cat' command which writes to
'/tmp/foobar' file:

	cat >/tmp/foobar<

Open 'README.md' file using 'ed', print the contents of lines 1-10 and then
replace the same lines with the buffer contents thereby allowing cursor
editing using 'ed' without code modifications to 'ed':

	ed README.md
	1,10p
	1,10c<.

## Files and directories

Opening files and directories is possible by prefixing command by a ":".
When opening directories have "/" after it to signify it is a directory.

Change to a parent directory:

	:../

Open a file:

	:README.md

## TODO

* Add more standard features like more Emacs bindings to the editor
  (most essential features are implemented)
* Bring the features from [iosplit](https://github.com/tleino/iosplit)
  to the editor such as overwriting previous command's output in an
  interactive session and launching new commands from a previous
  command's output (preliminary proof of concept is implemented, and
  a preliminary proof of concept is also implemented in form of slave
  buffers).
* Monitor file fd for changes when editing files so that changes to
  underlying file are not ignored.
* Distinguish slave buffers from non-slave buffers.

See also [vtsh issues](https://github.com/tleino/vtsh/issues).

## Known issues

* The "unsaved buffer" state is set even if only cursor is moved.

See also [vtsh issues](https://github.com/tleino/vtsh/issues).

## Unique keyboard focus system

*vtsh* can be used without mouse, unlike [acme](http://acme.cat-v.org/),
but *vtsh* has a unique way of how it can be used using keyboard. *vtsh*
has a two-level focus system:
* one level is the command tabs;
* another level is the output buffers.

Toggling between the level is possible using *Alt+Enter*, or
alternatively *Esc*.

## Noteworthy deviations

### From Emacs-like editors

* *Mark is not cleared if buffer is modified* while the mark is active,
instead the mark follows with the modifications. For clearing the mark,
use **Ctrl+g** or **Button 1**. This is useful because in *vtsh* buffers
might be updated on the fly e.g. if new content arrives from the shell
process.

### From normal virtual terminals

* The usual VTxxx/ANSI control sequences and ASCII control characters other
than TAB and NL *are not interpreted*. This is
useful because the buffers are editable. However, in the future it might
be possible that a separate "view-only" mode is added where these sequences
and characters are interpreted as expected.

## Using with mouse

* **Button 1** Move the cursor and/or deselect text (a.k.a. clear the mark).
* **Button 1 with motion** Select text (a.k.a. set the mark) and then move
the cursor.
* **Button 2** Paste text.
* **Button 3** Execute action from a context menu for a word under cursor,
or the whole line if the cursor is at or beyond EOL.
* **Scroll wheel** Scroll.

## Default key bindings

### Buffers
* **Alt+Up** Focus buffer above on the same level.
* **Alt+Down** Focus buffer below on the same level.
* **Alt+Enter** Toggle focus level.
* **Esc** Toggle focus level (alternative binding).
* **Alt+Backspace** Delete buffer.
* **Alt+Space** Add new buffer.
* **Alt+Insert** Add new buffer (alternative binding).
* **Alt+s** Add slave buffer (stdin will be send to master).
* **Alt+h** Hide/show buffer.
* **Shift+Alt+h** Hide other buffers (and show this if it was hidden).

### Editing

The bindings here are loosely-based on Emacs bindings, casual Emacs users
should feel at home.

* **Up/Down/Left/Right** Move cursor up/down/left/right.
* **Ctrl+p/n/b/f** Move cursor up/down/left/right (alternative).
* **Shift+Up/Down/Left/Right** Move cursor by 8 characters (in future will move by one word or one paragraph).
* **Enter** Add line or execute command / send stdin.
* **Ctrl+a** Move to beginning of line.
* **Ctrl+e** Move to end of line.
* **Ctrl+k** Kill to end of line (or remove line if line is empty).
* **Ctrl+d** Delete character under cursor.
* **Ctrl+l** Recenter.
* **Ctrl+o** Open line.
* **Backspace** Delete character on the left.
* **Ctrl+x Ctrl+s** Save file.

#### Search

* **Ctrl+s/r** Search forward/backward.
* **Ctrl+x g** Go to line number.
* **Ctrl+x Ctrl+g** Cancel action.
* **Ctrl+g** Cancel action.

#### Cut/paste and selections

* **Ctrl+space** Begin selection (a.k.a. set mark).
* **Ctrl+g** Clear selection.
* **Ctrl+w** Cut: Kill selected region.
* **Ctrl+y** Paste: Yank killed/copied region back.
* **Button 2** Copy/Paste: Copy marked region to mouse pointer location.

### Misc
* **Alt+n** Open new window.
* **Alt+q** Quit.
* **Alt+a** Dump object tree to stdout (only if -DDEBUG is enabled).

## Dependencies

* Unix-like POSIX-compliant environment with X Window System

The target is that it works on OpenBSD base system without requiring
additional packages. The OpenBSD base system has a good balance of
a standards compliant minimum installation that has just enough stuff
installed so that programs like this can be compiled without needing
to download external dependencies.

When compiling for other systems, you may need to install some random
dependencies such as C compiler and standard header files.

## Build and install to home directory

	$ ./configure ~
	$ make
	$ make install

## Customize colors and fonts

Take a look at
[color.enums](https://github.com/tleino/vtsh/blob/main/color.enums) and
[font.enums](https://github.com/tleino/vtsh/blob/main/font.enums), modify
and recompile.

*vtsh* supports both variable-width and fixed-width fonts (unlike *xterm*)
and supports the fontconfig FcPattern syntax as well as the X Logical Font
Description (XLFD).

You could try fonts such as:
* DejaVu Sans Mono:size=16.0
* sans serif *(variable width)*
* arial *(variable width)*
* -xos4-terminus-medium-r-normal--28-280-72-72-c-140-iso10646-1 *(usually
requires installing Terminus font package)*
* -misc-fixed-medium-r-normal--20-200-75-75-c-100-iso10646-1

These should be available in a modern X11 installation unless otherwise
noted. Change sizes to taste. You could also have a look at
[X Terminal TrueType Fonts](https://contented.qolc.net/articles/x-terminal-truetype-fonts/)
article written by Jamm!n.

(xrdb-compatible resources could be added later.)

## See also

* vtsh(1) is similar to the same author's cocovt(1) but can be used
standalone without support from the special cocowm(1) window manager.
It is also similar to iosplit(1) but that one relies on ncurses(3).
    * See [cocowm](https://github.com/tleino/cocowm).
    * See [cocovt](https://github.com/tleino/cocovt).
    * See [iosplit](https://github.com/tleino/iosplit).
* vtsh(1) is very similar to acme(1), except that vtsh(1)
    * Supports interactive sessions well, so well that it could be even used
      as a client to text-based Multi User Dungeons (in acme(1) the dislike
      for interactive sessions is more visible);
    * Relies on window manager for getting multiple columns.
    * Can be used without mouse.
* xd(1) is a proof-of-concept [edbrowse](https://github.com/CMB/edbrowse)-like
implementation for using e.g. Gemini "Small Internet" protocol that could
work very well with *vtsh*
    * See [xd](https://github.com/tleino/xd).
