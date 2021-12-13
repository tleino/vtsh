# vtsh

vtsh(1) is a mashup of *a virtual terminal* (vt) and a command interpreter
a.k.a. *shell* (sh) for Unix-like operating systems such as Linux. The
output of commands go to their own editable and viewable buffers,
the output can be used for launching new commands and the output
buffer can be used for interacting with the backend program in a
linear or non-linear fashion. It could also be thought *vtsh* means
*vertical tabs of shells*.

It also an exercise in taking the Unix philosophy to the extreme
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
* stateless request-response (e.g. *curl* requests to
HTTP REST API);
* stateless request-response with a long-lived response stream (e.g. *iostat*
configured to report status every second);
* **use slave buffers:** stateful request-response stream (e.g. *sh*, *telnet*
or *ed* session);
* **use inline input:** asynchronous request-response stream that may send
response at any random moment without a request (e.g. *sh* with background
commands, *mqtt* session or a chat session).

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

## TODO

* Add more standard features like more Emacs bindings to the editor
  (it is quite barebones at the moment).
* Bring the features from [iosplit](https://github.com/tleino/iosplit)
  to the editor such as overwriting previous command's output in an
  interactive session and launching new commands from a previous
  command's output (preliminary proof of concept is implemented, and
  a preliminary proof of concept is also implemented in form of slave
  buffers).
* Add support for e.g. '<file' to the command bar, so that files can be
  edited and saved with ease.
* Add support for some essential state such as current working directory
  that is necessary when not using an interactive shell session (current
  working directory is visible, but cannot be changed).
* Distinguish slave buffers from non-slave buffers.

## Unique keyboard focus system

*vtsh* can be used without mouse, unlike [acme](http://acme.cat-v.org/),
but *vtsh* has a unique way of how it can be used using keyboard. *vtsh*
has a two-level focus system:
* one level is the command tabs;
* another level is the output buffers.

Toggling between the level is possible using *Alt+Enter*, or
alternatively *Esc*.

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

### Editing
* **Up** Move cursor up.
* **Down** Move cursor down.
* **Left** Move cursor left.
* **Ctrl+b** Move cursor left (alternative).
* **Right** Move cursor right.
* **Ctrl+f** Move cursor right (alternative).
* **Enter** Add line or execute command / send stdin.
* **Ctrl+a** Move to beginning of line.
* **Ctrl+e** Move to end of line.
* **Ctrl+k** Kill to end of line (or remove line if line is empty).
* **Ctrl+d** Delete character under cursor.
* **Backspace** Delete character on the left.

### Misc
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

## Customize

* Take a look at
[color.enums](https://github.com/tleino/vtsh/blob/main/color.enums) and
[font.enums](https://github.com/tleino/vtsh/blob/main/font.enums), modify
and recompile.

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
