#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <features.h>

// Default configuration constants
#define DEFAULT_BUFFER_SIZE 20
#define DEFAULT_LOWER_THRESHOLD 0.4
#define DEFAULT_UPPER_THRESHOLD 0.7
#define HARD_UPPER_THRESHOLD 0.9
#define DEFAULT_SLEEP_TIME 1
#define DEFAULT_ACTOR_SLEEP_TIME 3
#define DEFAULT_PRODUCER_RATE 1

// Structure to represent a message with timestamp
typedef struct {
    int item;
    time_t timestamp;   
} Message;

// Global variables
Message *buffer;
int bufferSize;
double lowerThreshold, upperThreshold; // Thresholds for the buffer usage
int producerSleepTime, consumerSleepTime, actorSleepTime; // Sleep time for the producer, consumer, and actor
int producerRate; // Number of messages the producer try to add in a single iteration before going to sleep
int in = 0, out = 0, count = 0; // Indexes for the circular buffer 

// Utility global variables
int prodIdx = 0; // Counter for the total number of produced items
int consIdx = 0; // Counter for the total number of consumed items 
double totDelay = 0; // Sum of the delays of the consumed items
double minDelay = 0; // Min delay among the consumed items
double maxDelay = 0; // Max delay among the consumed items
volatile int tickCount = 0; // Tick count variable
volatile sig_atomic_t terminate = 0; // Flag for program termination
bool debug = false; // Flag for printing debug outputs

// Mutex 
pthread_mutex_t mutex; // Protects the access to the buffer
pthread_mutex_t producerRateMutex = PTHREAD_MUTEX_INITIALIZER; // Protects the access to the producer rate
pthread_mutex_t tickMutex = PTHREAD_MUTEX_INITIALIZER; // Protects the access to the tick count

// Semaphores
sem_t full, empty;

// Threads
pthread_t producerThread; // Thread for the producer
pthread_t consumerThread; // Thread for the consumer
pthread_t actorThread; // Thread for the actor
pthread_t tickThread; // Thread for counting the time
pthread_t inputThread; // Thread for managing user input during runtime

// Function prototypes
void *producer(void *arg); // Producer function
void *consumer(void *arg); // Consumer function
void *actor(void *arg); // Actor function
int produce_item(); // Generate an item 
void put_item(int item); // Insert an item in the buffer 
Message get_item(); // Remove an item from the buffer
void consume_item(Message item); // Consume (print) the item passed as argument
void adjust_production_rate(double queueUsage); // Adjust the production rate
void clean_resources(); // Clean up resources to terminate safely
void signal_handler(int signum); // Signal handler (for termination message upon specified timeout)

// Utility function prototypes
void printQueue(); // Print the queue content
void printOccupationAtTime(); // Print the queue occupation and production rate 
void* userInputListener(void *arg); // Function to remain in listen to user inputs
void* ticker_thread(void* arg); // Ticker thread to count the runtime duration 
void print_usage_guide(const char *programName); // Print how to use the program from command line

// Print how to launch the program from command line
void print_usage_guide(const char *programName) {
    printf("Usage: %s [options]\n", programName);
    printf("Options:\n");
    printf("\t--d  <debug>\t\t\t: Print the debug outputs (default:off)\n");
    printf("\t-bs  <buffer size (int)>\t: Set the buffer size (default: %d)\n", DEFAULT_BUFFER_SIZE);
    printf("\t-lt  <lower threshold (double)>\t: Set the lower threshold (default: %.2f)\n", DEFAULT_LOWER_THRESHOLD);
    printf("\t-ut  <upper threshold (double)>\t: Set the upper threshold (default: %.2f)\n", DEFAULT_UPPER_THRESHOLD);
    printf("\t-pst <prod. sleep time (int)>\t: Set the producer sleep time in seconds (default: %d)\n", DEFAULT_SLEEP_TIME);
    printf("\t-cst <cons. sleep time (int)>\t: Set the consumer sleep time in seconds (default: %d)\n", DEFAULT_SLEEP_TIME);
    printf("\t-ast <actor sleep time (int)>\t: Set the actor sleep time in seconds (default: %d)\n", DEFAULT_ACTOR_SLEEP_TIME);
    printf("\t-pr  <producer rate (int)>\t: Set the producer rate (default: %d)\n", DEFAULT_PRODUCER_RATE);
    printf("Example:\n");
    printf("\t%s -bs 50 -lt 0.3 -ut 0.7 -pst 2 -cst 1 -ast 2 -pr 3\n", programName);
}

// Print the parameters 
void print_parameters() {
    printf("\n----------------------------------------------------------\n");
    printf("RUNTIME PARAMETERS:\n");
    printf("  bufferSize          %d ", bufferSize);
    (bufferSize == DEFAULT_BUFFER_SIZE) ? printf("\t[default]\n") : printf("\n");
    printf("  lowerThreshold      %.2f ", lowerThreshold);
    (lowerThreshold == DEFAULT_LOWER_THRESHOLD) ? printf("\t[default]\n") : printf("\n");
    printf("  upperThreshold      %.2f ", upperThreshold);
    (upperThreshold == DEFAULT_UPPER_THRESHOLD) ? printf("\t[default]\n") : printf("\n");
    printf("  prodSleepTime       %d ", producerSleepTime);
    (producerSleepTime == DEFAULT_SLEEP_TIME) ? printf("\t[default]\n") : printf("\n");
    printf("  consSleepTime       %d ", consumerSleepTime);
    (consumerSleepTime == DEFAULT_SLEEP_TIME) ? printf("\t[default]\n") : printf("\n");
    printf("  actorSleepTime      %d ", actorSleepTime);
    (actorSleepTime == DEFAULT_ACTOR_SLEEP_TIME) ? printf("\t[default]\n") : printf("\n");
    printf("  producerRate        %d ", producerRate);
    (producerRate == DEFAULT_PRODUCER_RATE) ? printf("\t[default]\n") : printf("\n");
    printf("----------------------------------------------------------\n\n");
}

// Main Function
int main(int argc, char *argv[]) {
    // Initialize buffer size and other parameters at defaults
    bufferSize = DEFAULT_BUFFER_SIZE;
    lowerThreshold = DEFAULT_LOWER_THRESHOLD;
    upperThreshold = DEFAULT_UPPER_THRESHOLD;
    producerSleepTime = consumerSleepTime = DEFAULT_SLEEP_TIME;
    actorSleepTime = DEFAULT_ACTOR_SLEEP_TIME;
    producerRate = DEFAULT_PRODUCER_RATE;

    // Initialize and validate buffer size and other parameters based on command line arguments 
    for (int i = 1; i < argc; i++) {
        // Check the command line arguments for `--h`, `--help`, or `--usage`
        if (strcmp(argv[i], "--h") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--usage") == 0) {
            print_usage_guide(argv[0]);
            return 0; // Exit after printing help information
        } else if (strcmp(argv[i], "--d") == 0) {
            debug = true;
        } else if (strcmp(argv[i], "-bs") == 0) {
            if (i + 1 < argc) {
                bufferSize = atoi(argv[i + 1]);
            }
        } else if (strcmp(argv[i], "-lt") == 0) {
            if (i + 1 < argc) {
                lowerThreshold = atof(argv[i + 1]);
            }
        } else if (strcmp(argv[i], "-ut") == 0) {
            if (i + 1 < argc) {
                upperThreshold = atof(argv[i + 1]);
            }
        } else if (strcmp(argv[i], "-pst") == 0) {
            if (i + 1 < argc) {
                producerSleepTime = atoi(argv[i + 1]);
            }
        } else if (strcmp(argv[i], "-cst") == 0) {
            if (i + 1 < argc) {
                consumerSleepTime = atoi(argv[i + 1]);
            }
        } else if (strcmp(argv[i], "-ast") == 0) {
            if (i + 1 < argc) {
                actorSleepTime = atoi(argv[i + 1]);
            }
        } else if (strcmp(argv[i], "-pr") == 0) {
            if (i + 1 < argc) {
                producerRate = atoi(argv[i + 1]);
            }
        }
    }
    print_parameters(); // print the execution parameters

    int terminationMethod, timeoutDuration;
    printf("Available termination methods:\n");
    printf("1) Timeout\n");
    printf("2) Wait for X consumed messages\n");
    printf("3) Wait for 'q' and Enter\n");
    printf("4) Run indefinitely until CTRL+C :)\n");

    printf("\nEnter program timeout duration in seconds (enter 0 to run indefinitely): "); // Ask user for program timeout duration
    scanf("%d", &timeoutDuration);
    if(timeoutDuration < 0)
        timeoutDuration = 0;
    alarm(timeoutDuration);
    
    // Timeout before starting the actual execution
    for (int i=3; i>=0; i--) {
        printf("Prod-Cons starting in %d seconds\n", i);
        sleep(1); // Wait for one second
    }

    // Clear console before starting the actual execution
    printf("\e[1;1H\e[2J");

    // Initialize buffer, threads, mutex, and semaphores 
    printf("\n## ENVIRONMENT INITIALIZATION ##\n");
    printf("----------------------------------------------------------\n");
    // Dynamic allocation of the buffer
    buffer = malloc(bufferSize * sizeof(Message));
    if(buffer == NULL) {
        perror("Unable to allocate buffer");
        return EXIT_FAILURE;
    } else {
        for (int i = 0; i < bufferSize; i++) {
            bzero(&buffer[i], sizeof(Message));
            //buffer[i].item = 0; // Initialize the buffer to empty
            //buffer[i].timestamp = time(NULL);
        }
        printf("Buffer of size %d allocated and initialized to empty\n", bufferSize);
    }

    // Thread creations
    // Create input thread
    if(pthread_create(&inputThread, NULL, userInputListener, NULL) != 0) {
        perror("Failed to create input thread");
        return EXIT_FAILURE;
    } else {
        printf("Input thread created\n");
    }
        
    // Create producer thread
    if(pthread_create(&producerThread, NULL, producer, NULL) != 0) {
        perror("Failed to create producer thread");
        return EXIT_FAILURE;
    } else {
        printf("Producer thread created\n");
    }
    
    // Create consumer thread
    if(pthread_create(&consumerThread, NULL, consumer, NULL) != 0) {
        perror("Failed to create consumer thread");
        return EXIT_FAILURE;
    } else {
        printf("Consumer thread created\n");
    }
    
    // Create actor thread
    if(pthread_create(&actorThread, NULL, actor, NULL) != 0) {
        perror("Failed to create actor thread");
        return EXIT_FAILURE;
    } else {
        printf("Actor thread created\n");
    }

    // Initialize the mutex and the semaphores
    if(pthread_mutex_init(&mutex, NULL) != 0) {
        perror("Failed to initialize mutex thread");
        return EXIT_FAILURE;
    }
    if(sem_init(&full, 0, 0) != 0) { // Initially, 0 spaces are full
        perror("Failed to initialize full semaphore");
        return EXIT_FAILURE;
    } 
    if(sem_init(&empty, 0, bufferSize) != 0) { // Initially, all spaces are empty
        perror("Failed to initialize empty semaphore");
        return EXIT_FAILURE;
    }

    // Create and start the ticker thread
    if (pthread_create(&tickThread, NULL, ticker_thread, NULL) != 0) {
        perror("Failed to create ticker thread");
        return EXIT_FAILURE;
    } else {
        printf("Ticker thread created\n");
    } 
    printf("Semaphores and mutex succesfully created\n");
    printf("----------------------------------------------------------\n\n\n");


    // Set up signal handler to catch SIGALRM for program timeout
    signal(SIGALRM, signal_handler);
    alarm(timeoutDuration); // Schedule alarm to send SIGALRM after user specified timeout

    // Waiting with while loop for termination
    while (!terminate) {
        sleep(1); // Sleep for 1 second before checking the terminate flag
    }

    clean_resources(); // Clean resources before killing the program
    return EXIT_SUCCESS;
}

// Producer function
void *producer(void *arg) {
    sleep(3);
    while (true) {
        pthread_mutex_lock(&producerRateMutex);
        int localRate = producerRate;
        pthread_mutex_unlock(&producerRateMutex);

        for (int i = 0; i < localRate; i++) {
            int item = ++prodIdx;
            sem_wait(&empty);

            // Critical Section
            pthread_mutex_lock(&mutex); // Enter critical section
            // Check to ensure the buffer isn't full before adding
            if (count < bufferSize) {
                put_item(item); // Add one item to the buffer
                sem_post(&full); // Signal that an item was added
            }
            pthread_mutex_unlock(&mutex); // Exit critical section
        }
        
        sleep(producerSleepTime); // Sleep for producerSleepTime after attempting to produce `localRate` items
    }
    return NULL;
}

// Consumer function
void *consumer(void *arg) {
    sleep(3);
    while (true) {
        sem_wait(&full); // Decrements the full semaphore
        
        // Critical Section
        pthread_mutex_lock(&mutex); // Enter critical section 
        Message item = get_item(); // Retrieve one item from the buffer
        consume_item(item); // Consume the retrieved item
        sem_post(&empty); // Increments the empty semaphore
        pthread_mutex_unlock(&mutex); // Exit critical section
        sleep(consumerSleepTime); // Sleep for consumerSleepTime after consuming an item
    }
    return NULL;
}

// Actor function
void *actor(void *arg) {
    sleep(3);
    while (true) {
        sleep(actorSleepTime); // Sleep for actorSleepTime
        
        // Critical Section
        pthread_mutex_lock(&mutex); // Enter critical section
        double queueUsage = (double)count / bufferSize; // Compute the ratio of queue occupation over queue size
        pthread_mutex_unlock(&mutex); // Exit critical section

        // Adjust the production rate outside of the buffer mutex-protected section
        pthread_mutex_lock(&producerRateMutex); // Enter critical section for producerRate
        adjust_production_rate(queueUsage); // Adjust producerRate based on queue occupation
        pthread_mutex_unlock(&producerRateMutex); // Exit critical section for producerRate
    }
    return NULL;
}

// Produce an item
int produce_item() {
    return (rand() % (100 - 10)) + 10; // Generate a random item between [10, 99]
}

// Put an item in the buffer
void put_item(int item) { 
    buffer[in].item = item;
    buffer[in].timestamp = time(NULL);
    in = (in + 1) % bufferSize;
    count++;
    if(debug) {
        printf("Produced: %d", item);
        printQueue(); // Call to visualize the queue after produced an item
    }
    else {
        printOccupationAtTime();
    }
}

// Take an item from the buffer
Message get_item() { 
    Message item = buffer[out];
    bzero(&buffer[out], sizeof(Message));
    //buffer[out].item = 0; // Indicate that the position is now empty
    out = (out + 1) % bufferSize;
    count--;
    consIdx++;
    return item;
}

// Do something with the consumed item
void consume_item(Message item) { 
    if(debug) {
        double delay = difftime(time(NULL), item.timestamp); // Compute the delay for the current message
        totDelay += delay; // Update the total delay
        if(delay < minDelay) // Check if minDelay needs to be updated
            minDelay = delay;
        if(delay > maxDelay) // Check if maxDelay needs to be updated
            maxDelay = delay;

        printf("Consumed: %d", item.item);
        printQueue(); // Call to visualize the queue after consuming an item
    } else {
        printOccupationAtTime();
    }
}

// Adjust the production rate based on queue thresholds
void adjust_production_rate(double queueUsage) { 
    if (queueUsage < lowerThreshold && (count + producerRate) < bufferSize) {
        producerRate++; // Try to increment the production rate
        if(debug) 
            printf("Producer rate INCREMENTED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
    } else if ((queueUsage >= upperThreshold || (count + producerRate) >= HARD_UPPER_THRESHOLD) && producerRate >= 1) {
        producerRate--; // Try to decrement the production rate
        if(queueUsage >= HARD_UPPER_THRESHOLD && producerRate > 0) {
            producerRate--; // Try to decrement the production rate to avoid overflow
            if(debug)
                printf("Producer rate HARD DECREMENTED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
        }
        else {
            if(debug)
                printf("Producer rate SOFT DECREMENTED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
        }
    } else {
        if(debug)
            printf("Producer rate NOT CHANGED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
    }
}

// Clean up resources like threads, mutexes, semaphores, buffer... before exiting the program
void clean_resources() { 
    // Signal threads for cleanup and exit
    pthread_cancel(producerThread);
    pthread_cancel(consumerThread);
    pthread_cancel(actorThread);
    pthread_cancel(inputThread);

    // Join threads to ensure they are cleaned up
    pthread_join(producerThread, NULL);
    pthread_join(consumerThread, NULL);
    pthread_join(actorThread, NULL);
    pthread_join(inputThread, NULL);

    // Clean up threads, semaphores, and dinamically allocated memory
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&producerRateMutex);
    sem_destroy(&full);
    sem_destroy(&empty);
    free(buffer); 

    // Print runtime info before terminating
    printf("\n"); print_parameters();
    printf("----------------------------------------------------------\n");
    pthread_mutex_lock(&tickMutex);
    printf("Program ran for %d seconds.\n", tickCount);
    pthread_mutex_unlock(&tickMutex);
    printf("Number of produced items: %d\n", prodIdx);
    printf("Number of consumed items: %d\n", consIdx);
    printf("Average delay among the consumed items: %.3f\n", totDelay/consIdx);
    printf("Min delay among the consumed items: %.1f\n", minDelay);
    printf("Max delay among the consumed items: %.1f\n", maxDelay);
    printf("Number of items left in the queue: %d\n", count);
    int lostItems = prodIdx-consIdx;
    double percLostItems = ((double)lostItems / prodIdx)*100;
    printf("Number of items left in the queue: %d - Percentage: %.1f%% \n", lostItems, percLostItems);
    double prodRatio = (double)prodIdx / tickCount;
    double consRatio = (double)consIdx / tickCount;
    printf("Producer ratio over time (seconds): %.2f\n", prodRatio);
    printf("Consumer ratio over time (seconds): %.2f\n", consRatio);
    printf("----------------------------------------------------------\n\n");

    // Cancel the ticker thread
    pthread_cancel(tickThread); // Here we don't need to check since it arises error and return on failed init
    pthread_join(tickThread, NULL);   
}

// Signal handler to update terminate flag and terminate safely
void signal_handler(int signum) {
    // TODO Safe termination for signals like SIGINT (CTRL+C)
    terminate = 1;
}

// Print the current state of the buffer
void printQueue() {
    printf("\t|");
    for (int i = 0; i < bufferSize; i++) {
        if (buffer[i].item == 0) {
            printf("--|");
        } else {
            printf("%2d|", buffer[i].item);
        }
    }
    printf("\n");
}

int timeIdx = 1;
// Print the queue occupation and producer rate
void printOccupationAtTime() {
    printf("%d: %.2f %d\n", timeIdx++, (double)count / bufferSize, producerRate);
}

// Input listener to terminate on user input 'q'
void *userInputListener(void *arg) {
    char input;
    while (1) {
        input = getchar();
        if (input == 'q') {
            pthread_mutex_lock(&tickMutex); // by taking the lock mutex we assure ticker_thread is sleeping
            terminate = 1;
            pthread_mutex_unlock(&tickMutex);
            break;
        }
    }
    return NULL;
}

// Ticker thread that runs every second to increment the tick count
void* ticker_thread(void* arg) {
    while (terminate == 0) {
        pthread_mutex_lock(&tickMutex);
        tickCount++; // Increment the tick count variable
        pthread_mutex_unlock(&tickMutex);
        sleep(1); // Wait for 1 second
    }
    return NULL;
}

