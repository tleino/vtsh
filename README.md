# vtsh

vtsh(1) is a mashup of *a virtual terminal* and a command interpreter
a.k.a. *shell* for Unix-like operating systems such as Linux. The
output of commands go to their own editable and viewable buffers
and the output can be used for launching new commands.

It also an exercise in taking the Unix philosophy to the extreme
in the footsteps of e.g. acme(1) editor/user interface for
programmers that was originally created for the Plan 9 operating
system.

## The benefits of combining *vt* and *sh*

* can be used in place of e.g. xterm(1);
* can be used for running commands without launching a command interpreter
such as sh(1) or bash(1) in an interactive mode;
* can be used in place of a file browser such as mc(1) visual shell a.k.a.
Midnight Commander;
* can be used in place of a text user interface heavy e-mail program, such
as mutt(1) and a simpler one such as mail(1) or nail(1) can be used instead;
* etc.

## What it means

* there is no longer need for a separate editor, but e.g. ed(1) or even
cat(1) works fine;
* there is no longer need for a separate pager such as more(1) or less(1).
* there is no longer need for having cursor addressable text user
interfaces in standard I/O programs but they can be kept plain and simple.
This means there is no longer need for ncurses(3), terminfo(5), and all that.
* standard I/O programs get readline(3) like line editing functionality
and paging for free, but not only that, they also get full screen
editing support, so that e.g. ed(1) becomes an editor with full screen
editing capability without modifying a single line of code.

vtsh(1) enables simplicity by becoming one additional layer in the stack.
By doing that, it frees the *simple downstream programs from having to
reimplement cursor addressable user interfaces*, and as it does that, the
*user interface elements can be customized from a single place*.

## TODO

* Add standard features like basic Emacs bindings to the editor
  (it is very barebones at the moment)
* Bring the features from https://github.com/tleino/iosplit to the editor
  such as overwriting previous command's output in an interactive session
  and launching new commands from a previous command's output
* Add support for e.g. '<file' to the command bar, so that files can be
  edited and saved with ease
* Add support for some essential state such as current working directory
  that is necessary when not using an interactive shell session

## Default key bindings

* **Alt+Up** Focus buffer above
* **Alt+Down** Focus buffer below
* **Alt+Backspace** Delete buffer + focus buffer above
* **Alt+Insert** Add new buffer + focus it + edit its command
* **Up** Move cursor up
* **Down** Move cursor down
* **Left** Move cursor left
* **Right** Move cursor right
* **Enter** Add line or execute command / send stdin

## Build

	$ ./configure ~
	$ make
	$ make install

## See also

* vtsh(1) is similar to the same author's cocovt(1) but can be used
standalone without support from the special cocowm(1) window manager.
It is also similar to iosplit(1) but that one relies on ncurses(3).
    * See https://github.com/tleino/cocowm
    * See https://github.com/tleino/cocovt
    * See https://github.com/tleino/iosplit
* vtsh(1) is very similar to acme(1), except that vtsh(1)
    * supports interactive sessions well, so well that it could be even used
      as a "MUD client" (in acme(1) the dislike for interactive sessions is
      more visible);
    * relies on window manager for getting multiple columns.
* xd(1) is a proof-of-concept implementation of the Gemini "Small Internet"
  protocol that works especially well from a vtsh(1)-like interface
    * See https://github.com/tleino/xd
