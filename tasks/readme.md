This folder is reserved for TASK scripts.

Notable scripts:

- `pixel_bench.task`: exercises cached sprite drawing and reports pixel throughput.

Preferred folder structure:

./tasks/
|
|--./assets/       = Reserved for generic assets shared by all tasks
|--./script1/      = Dedicated assets related only to script1.task
|--./script1.task
|--./script2/      = Same as above...
|--./script2.task

Each task script should have it's own folder to store assets used by the script.


