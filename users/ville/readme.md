# NOTEBOOK - AALTO Development Diary #

This is my development diary for AALTO. Hope you'll enjoy it! Newest post is always on top.

### Friday, 28th of March 2025 ###

Added basic markup support to edit.c. The issue with multiline comments with C/C++ in edit.c seems to be a
tough cookie to solve. Could not do it today. The issue lies in the fact that the editor processes one 
terminal screen per time, and that can lead into obsucre number of /* and */ etc. comment markers per screen.

### Thursday, 27th of March 2025 ###

Modified the runtask RUN command to support any executable instead of only nodes. Simultaneously changed the
task names by stripping the node_ prefix, and created a task called "code.task", that opens three aalto
instances to tmux.
<br><br>
Created a command called "cls" to clear the terminal screen.
<br><br>
Improved the rss app with manual page changes and ability to quit the app by pressing "q".
<br><br>
Implemented following improvements to edit.c.:
- auto-indent, that works when pasting multiple rows as well
- changed the search (ctrl+f) to case-insensitive
- improved toggle so, that if user presses ctrl+c copy, it disables the toggle as a visual cue of the copy

### Wednesday, 26th of March 2025 ###

I decided to add some FUN to the equation and provided a games/ folder with space invaders clone using ASCII
graphics. In addition, I edited the makefile to exclude the user folder from compilation. With the addition
of "compile" command, users are able to build their own C applications to user folders if desired. Implemented
a huge speed improvement to edit.c when pasting large files.

### Sunday, 23rd of March 2025 ###

Today the project took a turn towards professionalism. Created a proper readme.md for the git repo, with
licenses, dependencies and instructions how to install and run AALTO. Also, I managed to improve many commands
and create few more, from which maybe the "run" app provides most of the value, as it extends the user with the
capability to run any normal linux executable and/or command through AALTO. My intention is still to keep AALTO
as curated environment, where all the "approved" apps are installed through setup.sh.
<br><br>
To make AALTO usable, I need to develop a node called "node_code", which basically opens the server and few tmux 
windows with plain AALTO running in them parallel. This way I can start experimenting how the tools are suited for
programming.

### Saturday, 22nd of March 2025 ###

It was a beautiful day and I decided to spend the morning of it at libary to implement following improvements.
- app: inet; interactive internet connection management tool.
- cmd: list; added regexp like capabilities and help. however, a persisting BUG of main.c app not displaying the full
             paged result. cannot find the reason for this bug.
- app: edit; added the search functionality from main.c paging

### Friday, 21st of March 2025 ###

Created a very primitive login functionality, basically just to guide the user into his working directory. No password
protection whatsoever. Implemented also a new command called "cmath", that is capable of basic math operations, and
running macros by typing "cmath mymacro.m". Added also up/down command history for input.c. Implemented a horizontal
position "remembering" to edit.c, where if user presses up/down arrows, the cursor will remain to same horizontal 
position only if it is possible, otherwise it will fall to the end of line.

### Sunday, 16th of March 2025 ###

Today I refactored the whole project structure to resemble more like OS. Also, my intention is to start replacing my
normal terminal use with AALTO more and more to test it's functionality and suitability to the use it has been designed
for. Already I can see, that there are few lacking capabilities in edit.c, e.g. when up and down arrows are used, the
cursor moves straight path up and down instead of following the text entries per line. The startup needs some kind of
function that queries the user that is logging in and initializes the view to the correct user folder.


