# Producer-Consumer with rate adjustment
A single producer - single consumer with production rate adjustment implementation in C using threads and semaphores.

An additional thread "actor" has been implemented to monitor the buffer, decide if it is underused, overused, or none, and adjust the production rate.
