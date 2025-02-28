Project: All Around Linux Terminal Operator (AALTO)<br>
Author: Ville Suoranta<br>
===============================================================================<br>
Dependencies:<br>
- tmux (required to use the RUN and START commands in TASK language)<br>
<br>
Build:<br>
run "make"<br>
===============================================================================<br>
Description:<br>
<br>
Plain -std=c11 POSIX compatible automation engine aimed for scientific and <br>
engineering purposes. Capabilities:<br>
<br>
- TASK language to automate running of applications<br>
  Example: ./aalto mytask.task<br>
- Universal switchboard server that can connect apps together using 5 standard<br>
  inputs and outputs (see example: app_template.c)<br>
<br>
===============================================================================<br>
Project Struture:<br>
- root = all the main source codes that utilize includes from commands and apps<br>
- commands/ = folder containing all the commands AALTO can execute<br>
- apps/ = folder containing all the apps TASK language can run<br>
<br>
===============================================================================<br>
tmux help:<br>
- CTRL+B+A+D exits the generated tmux session by RUN/START commands<br>
<br>
