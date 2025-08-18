# NOTEBOOK - BUDOSTACK Development Diary #

This is my development diary for BUDOSTACK. Hope you'll enjoy it! Newest post is always on top.

### Monday, 18th of August 2025 ###

I created a more suitable format for my development diary and started hosting it in github
as a website using git's pages feature. It loads the readme.md hosting the diary and parses
it as individual blog entries.

### Sunday, 17th of August 2025 ###

Improved today edit.c and libedit.c using OpenAI's CODEX. The agentic AI creates an environment
where it is able to analyze and run the budostack source code. It seems to be able to test the
changes as well, and then create a PULL request to git to be reviewed. Interesting times we are
living...

### Saturday, 9th of August 2025 ###

Ok, GPT 5 by OpenAI was release few days ago. My long time trouble app edit.c was the chosen 
benchmark to test whether it really is worth of the hype. It seems, that with first try it was
able to fix some long term bugs that none of the earlier models were able to fix. The future is
in creativity and prompts.

### Monday, 16th of May 2025 ###

It has been a while since my last update. However, just recently OpenAI launched their new feature
called "CODEX", which enables one connecting github account and selected repositories to chatgpt.
I had to test it out, and as a result I created two capabilities to BUDOSTACK edit app: fist one
being the time/date top bar, and second being the capability to copy -> paste stuff from editor
to 3rd part apps, such as web browser etc. Also, fixed the CTRL+F search mode so that the search
prompt is always displayed on top of screen.

### Friday, 9th of May 2025 ###

It has been a while since last diary entry. However, today I felt enough inspired to continue this
project. I started the dev. by improving the 'compile' command so that it accepts compile flags
such as '-lm' etc.. Then I continued by creating a MVP csvplot app that allows user to visualize
in ASCII format csv columns in x-y format. Found a bug from cmath when assigning arrays and printing
them, fixed that. Found an issue with main.c paging, depending on the page breaks it can lose rows,
or not display them. Fixed that as well.

### Wednesday, 30th of April 2025 ###

Did a minor change to slides.c; where if the user presses SPACE or BACKSPACE, the editing behavior
is intuitive and similar to traditional text editors. However, there still is room for improvement,
e.g. when user types it automatically replaces characters instead of appending the to the line.
A future development effort required to fix that.

### Thursday, 17th of April 2025 ###

Introduced three new commands: crc32, csvstat, csvclean. All have -help and are self explanatory.

### Tuesday, 15th of April 2025 ###

Today I rebranded my software from AALTO to BUDOSTACK, as latter was not reserved trademark. Since my
last diary entry I have implemented categorization of apps and commands to help, and few cool science
apps like skydial and solar. Also, I made the cmath more usable so, that it supports ";" for non-echoing
commands and "print". I did the conversion using my own commands and tools, and it proved to be easy and
fast!
<br><br>
Modified commandparser.c to exclude the wildcard expansion from pre-defined commands. Do not know whether
this is the best approach, need to find out by using the software more to different purposes. But now at
least the list.c is able to find files from subdirectories.

### Friday, 11th of April 2025 ###

This turned to be quite cool idea; slides.c - a terminal based ASCII powerpoint that let's the user to
focus only to the content! Released it to the main branch today.

### Thursday, 10th of April 2025 ###

Improved table.c quite much. Modifications include bugfixes to handling references like $B$4 and cell
higlighting when the content of the cell is wider than the cell. Put the help bar behind CTRL+T toggle.
Created an example spreadsheet to the default user folder. Animated the cls.c and transferred it from
commands folder to apps folder as it is not a blocking app.

### Tuesday, 8th of April 2025 ###

Took a task to improve table.c app towards more excel like capabilities. Successfully introduced quite
functional spreadsheet app with basic math and cell referencing. However, there is one build warning and
bug that adds an empty column to loaded file that needs to be fixed later on...

### Monday, 7th of April 2025 ###

Ok, did an inspection to main.c, commandparser.c and input.c and came to a conclusion that to have run
-like command, it is better to include it into the main.c and delete the separate run.c. Now user should
be able to run any linux shell like command using run.
<br><br>
Modified main.c and commandparser.c so that AALTO cannot be anymore killed using CTRL+C, but all the apps
started within AALTO can. This makes a bit more authentic OS feeling to AALTO if it would be started in
machine bootup automatically.
<br><br>
Introduced 'restart' command with 'restart -f' option that re-compiles AALTO and restarts it. It can be
ran from any folder and the latter option will clean the build before re-compiling it. Maybe later on I
need to do a complete routine from 'restart', that zips the current version to backup/ before building.
If the build fails, the working version is restored from the backup...

### Saturday, 5th of April 2025 ###

Found today, that edit.c had still an issue when pasting content from external source using ctrl+shift+v.
Needed to iterate quite a lot to figure out what is the correct way to handle pasting from ext. sources.
Proved to be the "bracketed pasting", as when ctrl+shift+v is used, terminal will inject the clipboard
to the input stream hence making the editor to think that user is typing normally the content in. This
caused the auto-indent feature to keep adding indents skewing the whole pasted content. Modern terminals
however use a certain escape syntax to indicate when content is being pasted from ext. source. When taking
that approach into use, it finally fixed the issue.
<br><br>
Implemented also a new command called "mdread" that can be used to pretty print .md files to terminal for
easier readability (similar how github displays them).
<br><br>
Next step is to check the whole main.c and related command handling. The project has changed since the
beginning, and I think the original way of having main.c, input.c and commandparser.c handling the input
is not anymore fit to purpose, as all the apps and commands are stand-alone executables. The aim would
be to remove the commandparser.c and input.c and have only the main.c.

### Wednesday, 2nd of April 2025 ###

**edit.c:**<br>
Ok, I had to delete my post from yesterday as it was proven to be a human error. I had configured my GPT
to produce gibberish. After changing my customization settings for o3-mini-high, it succeeded with first 
attempt and produced a high-quality and robust multi-line comment detection with one bug only; row numbers 
marked with green color from the region of the multi-line comment. The second prompt with bug report 
provided gave me fix to that as well. Decided to implement auto-indent also, that is disabled when pasting 
content.
<br><br>
**Next Steps:**<br>
The AALTO system has reached a point where it has all kinds of apps and commands. Next I will try to start
using them and fine-tuning them as usable as possible instead of adding more apps and commands. However, I
cannot promise that I do not sneak in a game here and there. You can follow the progress of this from the
git commit logs, it's to tedious to report them separately to this dev. diary.

### Saturday, 29th of March 2025 ###

Came up with a funny little app called ctalk, an UDP based LAN messaging app to which users can dynamically
log in and have IRC like discussions with nicknames. Is able to keep up the chat as long as the user acting
as a server is online. In the future intention is to add dynamic server switching in case the user with the
running server decides to quit the chat.
<br><br>
Started also an app called "share", which is a collaborative tool that syncs a file to multiple PCs in LAN
using UDP. Got the basics to work, but need to add "edit mysharedfile.c - live" mode to edit.c so that when
collaborating, it would periodically refresh the file from disk to update the changes done by other users
contributing to the same file in the same session.
<br><br>
Established a wish list on top of this development diary to keep the ideas that interest me the most
visible for everybody.

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


