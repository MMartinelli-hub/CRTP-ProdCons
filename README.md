# Producer-Consumer with Dynamic Rate Adjustment
---

A single producer - single consumer with dynamic production rate adjustment implementation in C using threads and semaphores.

An additional thread "actor" has been implemented to monitor the buffer, decide if it is underused, overused, or none, and adjust the production rate.

The adjustments can be of four types:
1. Increment: increment the producer rate of 1
2. Soft Decrement: decrement the producer rate of 1
3. Hard Decrement: decrement the producer rate of 2
4. Don't change: leave the producer rate as it is
---
## Usage Guide
To compile the program:

```bash
gcc project.c -o project
```

To execute the program from the command line:

```bash
./project [options]
```

Options:

    -bs <buffer size (int)>: Set the buffer size (default: DEFAULT_BUFFER_SIZE)
    -lt <lower threshold (double)>: Set the lower threshold (default: DEFAULT_LOWER_THRESHOLD)
    -ut <upper threshold (double)>: Set the upper threshold (default: DEFAULT_UPPER_THRESHOLD)
    -pst <producer sleep time (int)>: Set the producer sleep time in seconds (default: DEFAULT_SLEEP_TIME)
    -pst <consumer sleep time (int)>: Set the consumer sleep time in seconds (default: DEFAULT_SLEEP_TIME)
    -ast <actor sleep time (int)>: Set the actor sleep time in seconds (default: DEFAULT_ACTOR_SLEEP_TIME)
    -pr <producer rate (int)>: Set the producer rate (default: DEFAULT_PRODUCER_RATE)

Example:

```bash
./project -bs 50 -lt 0.6 -ut 0.7 -pst 1 -cst 1 -ast 2 -pr 3
```
---
## Validation of Input Parameters
The input parameters need to respect the following properties:
    
    -bs <buffer size (int)>: must be an integer > 0; however the code works better with > 10  
    -lt <lower threshold (double)>: must be a double in the interval [0, 1)
    -ut <upper threshold (double)>: must be a double in the interval (0, 1] greater than the lower threshold 
    -pst <producer sleep time (int)>: must be an integer
    -pst <consumer sleep time (int)>: must be an integer
    -ast <actor sleep time (int)>: must be an integer
    -pr <producer rate (int)>: must be an integer

It's important to notice some aspects:
1. To have more freedom in the simulations, no actual validation is performed on the input parameters, so it is up to the user to respect what is defined above;
2. If only the upper threshold is specified, the lower threshold is set as (upperThreshold - 0.1). 
If that behavior is not wanted, the user should explicitly define also the lower threshold.
3. For the program's correct behavior, the upper threshold in buffer utilization should be directly proportional to the buffer size, namely the higher the size the nearest we can place the upper threshold to 1.
However, the recommended interval is [0.7,0.9)
---
