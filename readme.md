/* All-Around Linus Terminal Operator - AALTO */  
Programmed by: Ville Suoranta (and mr. AI)  
  
Available Commands:  
  
  help     : Display this help message.  
  list     : List contents of a directory (default is current directory).  
  display  : Display the contents of a file.  
  copy     : Copy a file from source to destination.  
  move     : Move (rename) a file from source to destination.  
  remove   : Remove a file.  
  update   : Create an empty file or update its modification time.  
  makedir  : Create a new directory.  
  rmdir    : Remove an empty directory.  
  runtask  : Run a proprietary .task script until CTRL+c is pressed.  
             Type: runtask -help for more details.  
  stats    : Displays basic hardware stats.  
  exit     : Exit AALTO.  
  
Running Applications:  
  
  All applications are stored in the apps/ folder and they can be ran only via TASK scripting.  
  
  Different types of applications:  
  
  server   : A switchboard server that uses route.rt to route client application inputs and outputs.  
  cl_<app> : A client application that can have up to 5 inputs and 5 outputs.  
             Requires the server running before starting.  
  <app>    : Normal application that does not provide any inputs or outputs to be routed.  
  
  Tips:  
  Start AALTO faster: ./aalto -f | Start TASK from cmd line: ./aalto mytask.task  
  
