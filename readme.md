Project: All Around Linux Terminal Operator (AALTO)
Author: Ville Suoranta
===============================================================================
Dependencies:
- tmux (required to use the RUN and START commands in TASK language)

Build:
run "make"
===============================================================================
Description:

Plain -std=c11 POSIX compatible automation engine aimed for scientific and 
engineering purposes. Capabilities:

- TASK language to automate running of applications
  Example: ./aalto mytask.task
- Universal switchboard server that can connect apps together using 5 standard
  inputs and outputs (see example: app_template.c)

===============================================================================
Project Struture:
- root = all the main source codes that utilize includes from commands and apps
- commands/ = folder containing all the commands AALTO can execute
- apps/ = folder containing all the apps TASK language can run

===============================================================================
tmux help:
- CTRL+B+A+D exits the generated tmux session by RUN/START commands
  
