# BUDOSTACK - The Martial Art of Software
**Programmed by:** Ville Suoranta, by using OpenAI tools.<br>
**Status:** Early Access (in development)

Check out development diary from [HERE](https://github.com/sensei-zenabi/BUDOSTACK/blob/main/users/ville/readme.md)

### Licence:
BUDOSTACK is distributed under GPL-2.0 license, which is a is a free 
copyleft license, that allows you to:
- Run the software for any purpose
- Study and modify the source code
- Redistribute copies, both original and modified, provided you will 
distribute them under the same GPL-2.0 terms and include the source 
code.

## Description:
A lightweight operating "layer" built atop POSIX-compliant Linux, 
specifically designed for those who value the elegant simplicity 
and clarity found in operating systems of the 1980s. Optimized for 
maximum focus and efficiency on basic primitives of computing, 
such as file manipulation, text editing, command-line interactions, 
and efficient resource management.

## Dependencies
To successfully run BUDOSTACK, **Debian** based Linux distributions
are recommended. They provide the "sudo apt install" as default, which
is used e.g. in the setup.sh shell script to install applications 
utilized by BUDOSTACK to execute it's apps and commands. During the  
development of BUDOSTACK, it has been tested with following distros:
- Ubuntu
- Kubuntu
- Raspberry Pi OS

## How to Install and Run?
1. Checkout the repo
2. Run the ./setup.sh shell script
3. Type "./budostack -f" for fast start
4. Then type "help"

## How to Run Tasks?
Nodes are being built using the proprietary TASK language. Example
nodes can be found from the "tasks" -folder and instructions from
TASK language can be seen by typing "runtask -help" within BUDOSTACK.
<br><br>
Below is an example how to start a node:
- ./budostack code      (when starting from linux terminal)
- runtask code.task (when starting from within BUDOSTACK)
