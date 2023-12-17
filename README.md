# Producer-Consumer with rate adjustment
A single producer - single consumer with production rate adjustment implementation in C using threads and semaphores.

An additional thread "actor" has been implemented to monitor the buffer, decide if it is underused, overused, or none, and adjust the production rate.

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
    -st <sleep time (int)>: Set the sleep time in seconds (default: DEFAULT_SLEEP_TIME)
    -ast <actor sleep time (int)>: Set the actor sleep time in seconds (default: DEFAULT_ACTOR_SLEEP_TIME)
    -pr <producer rate (int)>: Set the producer rate (default: DEFAULT_PRODUCER_RATE)

Example:

```bash
./project -bs 50 -lt 0.6 -ut 0.7 -st 1 -ast 2 -pr 3
```
