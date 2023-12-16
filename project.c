#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

// Default configuration constants
#define DEFAULT_QUEUE_SIZE 10
#define DEFAULT_LOWER_THRESHOLD 0.3
#define DEFAULT_UPPER_THRESHOLD 0.7
#define DEFAULT_SLEEP_TIME 1
#define DEFAULT_ACTOR_SLEEP_TIME 3
#define DEFAULT_PRODUCER_RATE 1

// Global variables
int *buffer;
int bufferSize;
double lowerThreshold, upperThreshold;
int sleepTime, actorSleepTime, producerRate;
int in = 0, out = 0, count = 0;
int increaseRateCounter = 0; // Counters to track consecutive deviations
int decreaseRateCounter = 0;
int adjustmentCount = 3; // A threshold for consecutive counts before adjusting

// Condition variable for termination
pthread_cond_t condTerminate = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutexTerminate = PTHREAD_MUTEX_INITIALIZER;

// Added flag for program termination
volatile sig_atomic_t terminate = 0;

// Mutex and Semaphores
pthread_mutex_t mutex;
sem_t full, empty;

// Function prototypes
void *producer(void *arg);
void *consumer(void *arg);
void *actor(void *arg);
int produce_item();
void put_item(int item);
int get_item();
void consume_item(int item);
void adjust_production_rate(double queueUsage);
void fineAdjust_production_rate(double queueUsage);
void printQueue();

// Input listener to terminate on user input 
void *userInputListener(void *arg) {
    char input;
    while (1) {
        input = getchar();
        if (input == 'q') {
            pthread_mutex_lock(&mutexTerminate);
            terminate = 1;
            pthread_cond_signal(&condTerminate);
            pthread_mutex_unlock(&mutexTerminate);
            break;
        }
    }
    return NULL;
}

void clean_resources() {
    // Clean up resources like threads, mutexes, semaphores, buffer...
}

// Signal handler to update terminate flag
void signal_handler(int signum) {
    // Safe termination for signals like SIGINT (CTRL+C)
    terminate = 1;
}

int main(int argc, char *argv[]) {
    // Ask user for program timeout duration
    int timeoutDuration;
    printf("Enter program timeout duration (seconds): ");
    scanf("%d", &timeoutDuration);
    for (int i=3; i>=0; i--) {
        printf("Prod-Cons starting in %d seconds\n", i);
        sleep(1); // Wait for one second
    }
    // Clear console before starting the actual execution
    printf("\e[1;1H\e[2J");

    // Set up signal handler to catch SIGALRM for program timeout
    signal(SIGALRM, signal_handler);
    alarm(timeoutDuration); // Schedule alarm to send SIGALRM after user specified timeout
    
    // Initialize buffer size and other parameters based on command line arguments or defaults
    bufferSize = (argc > 1) ? atoi(argv[1]) : DEFAULT_QUEUE_SIZE;
    lowerThreshold = (argc > 2) ? atof(argv[2]) : DEFAULT_LOWER_THRESHOLD;
    upperThreshold = (argc > 3) ? atof(argv[3]) : DEFAULT_UPPER_THRESHOLD;
    sleepTime = (argc > 4) ? atoi(argv[4]) : DEFAULT_SLEEP_TIME;
    actorSleepTime = (argc > 5) ? atoi(argv[5]) : DEFAULT_ACTOR_SLEEP_TIME;
    producerRate = (argc > 6) ? atoi(argv[6]) : DEFAULT_PRODUCER_RATE;

    // Dynamic allocation of the buffer
    buffer = malloc(bufferSize * sizeof(int));

    if(buffer == NULL) {
        perror("Unable to allocate buffer");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < bufferSize; i++) {
        buffer[i] = 0; // Initialize the buffer to empty
    }

    // Initialize the mutex and the semaphores
    pthread_mutex_init(&mutex, NULL);
    sem_init(&full, 0, 0); // Initially, 0 spaces are full
    sem_init(&empty, 0, bufferSize); // Initially, all spaces are empty

    // Thread declarations
    pthread_t producerThread, consumerThread, actorThread;

    // Thread creations
    pthread_create(&producerThread, NULL, producer, NULL);
    pthread_create(&consumerThread, NULL, consumer, NULL);
    pthread_create(&actorThread, NULL, actor, NULL);

    /*
    // Wait for the threads to finish
    pthread_join(producerThread, NULL);
    pthread_join(consumerThread, NULL);
    pthread_join(actorThread, NULL);
    */

    // Waiting with while loop instead of pthread_join
    while (!terminate) {
        sleep(1); // Sleep for 1 second before checking the terminate flag
    }

    // Signal threads for cleanup and exit
    pthread_cancel(producerThread);
    pthread_cancel(consumerThread);
    pthread_cancel(actorThread);

    // Join threads to ensure they are cleaned up
    pthread_join(producerThread, NULL);
    pthread_join(consumerThread, NULL);
    pthread_join(actorThread, NULL);

    // Clean up
    pthread_mutex_destroy(&mutex);
    sem_destroy(&full);
    sem_destroy(&empty);
    free(buffer);

    printf("Program has concluded after %d seconds.\n", timeoutDuration);
    return EXIT_SUCCESS;
}


// Producer function
void *producer(void *arg) {
    while (true) {
        int item = produce_item();
        sem_wait(&empty); // Decrements the empty semaphore
        pthread_mutex_lock(&mutex);

        // Critical section
        for (int i = 0; i < producerRate && count < bufferSize; i++) {
            put_item(item);
            item = produce_item();
        }

        pthread_mutex_unlock(&mutex);
        sem_post(&full); // Increments the full semaphore
        sleep(sleepTime); // Sleep for specified time
    }
    return NULL;
}


// Consumer function
void *consumer(void *arg) {
    while (true) {
        sem_wait(&full); // Decrements the full semaphore
        pthread_mutex_lock(&mutex);

        // Critical section
        int item = get_item();
        consume_item(item);
        pthread_mutex_unlock(&mutex);
        sem_post(&empty); // Increments the empty semaphore
        sleep(sleepTime); // Sleep for specified time
    }
    return NULL;
}

void printQueue() {
    // Added function to print the current state of the buffer
    printf("\t|");
    for (int i = 0; i < bufferSize; i++) {
        if (buffer[i] == 0) {
            printf("--|");
        } else {
            printf("%d|", buffer[i]);
        }
    }
    printf("\n");
}

// Actor function
void *actor(void *arg) {
    while (true) {
        pthread_mutex_lock(&mutex);
        // Critical section
        double queueUsage = (double)count / bufferSize;

        // Adjust the production rate based on queue usage
        adjust_production_rate(queueUsage);
        pthread_mutex_unlock(&mutex);
        sleep(actorSleepTime); // Sleep for specified time
    }
    return NULL;
}


// Produce an item
int produce_item() {
    // Generate a random item between [10, 99]
    return (rand() % (100 - 10)) + 10;
}


void put_item(int item) { // Put an item in the buffer
    buffer[in] = item;
    in = (in + 1) % bufferSize;
    count++;
    printf("Produced: %d", item);
    printQueue(); // Call to visualize the queue after produced an item
}


int get_item() { // Take an item from the buffer
    int item = buffer[out];
    buffer[out] = 0; // Indicate that the position is now empty
    out = (out + 1) % bufferSize;
    count--;

    return item;
}


void consume_item(int item) { // Do something with the consumed item
    printf("Consumed: %d", item);
    printQueue(); // Call to visualize the queue after consuming an item
}


void adjust_production_rate(double queueUsage) { // Adjust the production rate based on queue thresholds
    if (queueUsage < lowerThreshold && (count + producerRate + 1) < bufferSize) {
        producerRate++; // Try to increment the production rate
        printf("Producer rate INCREMENTED to: %d - Queue usage: %d - Free: %d\n", producerRate, count, bufferSize-count);
    } else if (queueUsage > upperThreshold && producerRate >= 1) {
        producerRate--; // Try to decrement the production rate
        if(queueUsage >= 0.9 && producerRate > 0) {
            producerRate--; // Try to decrement the production rate to avoid overflow
            printf("Producer rate HARD DECREMENTED to: %d - Queue usage: %d - Free: %d\n", producerRate, count, bufferSize-count);
        }
        else {
            printf("Producer rate SOFT DECREMENTED to: %d - Queue usage: %d - Free: %d\n", producerRate, count, bufferSize-count);
        }
    }
    // Implement additional safety mechanisms as needed
}

void fineAdjust_production_rate(double queueUsage) {
    // Keeping track of the old rate to compare changes
    int oldRate = producerRate;
    char *changeType = "soft"; // Assume a 'soft' change by default

    if (queueUsage < lowerThreshold && (count + producerRate < bufferSize)) {
        increaseRateCounter++;
        decreaseRateCounter = 0;
        
        if (increaseRateCounter >= adjustmentCount) {
            producerRate += (lowerThreshold - queueUsage > lowerThreshold * 0.5) ? 2 : 1;
            if (count + producerRate > bufferSize) {
                producerRate = bufferSize - count; // Ensure no buffer overflow
                changeType = "hard"; // This is a hard change to prevent overflow
            }
            increaseRateCounter = 0;
            printf("Production rate %s incremented from: %d to: %d - reason: below lower threshold - queue count: %d\n", 
                   changeType, oldRate, producerRate, count);
        }
    } else if (queueUsage > upperThreshold && producerRate > 1) {
        decreaseRateCounter++;
        increaseRateCounter = 0;

        if (decreaseRateCounter >= adjustmentCount) {
            producerRate -= (queueUsage - upperThreshold > upperThreshold * 0.5) ? 2 : 1;
            /*
            if (producerRate < 1) {
                producerRate = 1; // Prevent the production rate from going below 1
                changeType = "hard"; // This is a hard change to prevent underflow
            }
            */
            decreaseRateCounter = 0;
            printf("Production rate %s decremented from: %d to: %d - reason: above upper threshold - queue count: %d\n", 
                   changeType, oldRate, producerRate, count);
        }
    } else {
        // If no change to rate, print message indicating no change.
        printf("Production rate %d not changed - reason: within thresholds - queue count: %d\n", producerRate, count);
        // Reset both counters if the usage is within the threshold
        increaseRateCounter = 0;
        decreaseRateCounter = 0;
    }
}



