# Credit-Scheduler
A priority based threads scheduler

## The modification
The main change is in gt_matrix.c, gt_uthread.c and gt_kthread.c. The details is discussed in the report.

## How to run
A script  named 'run.sh' can be used to compile and run the program in credit scheduler mode. Or you can use the command

```
make clean
make 
make matrix
./bin/matrix 0  ## default priority scheduler
./bin/matrix 1  ## credit scheduler
```

## Output
Two output files are provided. output_2core_ns is tested in the local machine.output_4core_s is tested in the provided shared vm.
