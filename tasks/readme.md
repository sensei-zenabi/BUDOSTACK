This folder is reserved for TASK scripts.

TASK scripts are intended to work like Windows 95-style icons: users can create
or copy `.task` launchers here to start proprietary applications, launch
existing tools, schedule operating-system work, or bundle small workflows.

Preferred folder structure:

./tasks/
|
|--autoexec.task       = Startup TASK that runs when BUDOSTACK starts
|--examples/           = Bundled example TASK scripts
|  |--assets/          = Assets used by bundled example TASK scripts
|  |--sprites.task
|  |--colors.task
|--myapp/              = Dedicated assets related only to myapp.task
|--myapp.task
|--tools/              = Optional subfolder for user TASK launchers
|  |--backup.task

`runtask` can launch any `.task` file under this directory tree from any
working directory. Examples:

* `runtask myapp.task`
* `runtask screen.task` (finds the bundled example recursively)
* `runtask tools/backup.task`
* `runtask examples/colors.task`

Each task script should have its own folder when it needs private assets.
