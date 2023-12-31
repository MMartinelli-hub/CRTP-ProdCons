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
#define DEFAULT_BUFFER_SIZE 10
#define DEFAULT_LOWER_THRESHOLD 0.3
#define DEFAULT_UPPER_THRESHOLD 0.7
#define DEFAULT_SLEEP_TIME 1
#define DEFAULT_ACTOR_SLEEP_TIME 3
#define DEFAULT_PRODUCER_RATE 1

// Global variables
int *buffer;
int bufferSize;
double lowerThreshold, upperThreshold; // Thersholds for the buffer usage
int sleepTime; // Sleep time for the producer and the consumer
int actorSleepTime; // Sleep time for the actor
int producerRate; // Number of messages the producer try to add in a single iteration before going to sleep
int in = 0, out = 0, count = 0; // Indexes for the circular buffer
int increaseRateCounter = 0; // Counters to track consecutive deviations
int decreaseRateCounter = 0;

// Utility global variables
int adjustmentCount = 3; // A threshold for consecutive counts before adjusting
int prodIdx = 0; // Counter for the total number of produced items
int consIdx = 0; // Counter for the total number of consumed items 
volatile int tickCount = 0; // Tick count variable
volatile sig_atomic_t terminate = 0; // Flag for program termination

// Mutex 
pthread_mutex_t mutex; // Protects the access to the buffer
pthread_mutex_t producerRateMutex = PTHREAD_MUTEX_INITIALIZER; // Protects the access to the producer rate
pthread_mutex_t tickMutex = PTHREAD_MUTEX_INITIALIZER; // Protects the access to the tick count

// Semaphores
sem_t full, empty;

// Threads
pthread_t inputThread; // Thread for managing user input during runtime
pthread_t tickThread; // Thread for counting the time
pthread_t producerThread; // Thread for the producer
pthread_t consumerThread; // Thread for the consumer
pthread_t actorThread; // Thread for the actor

// Function prototypes
void *producer(void *arg); // Producer function
void *consumer(void *arg); // Consumer function
void *actor(void *arg); // Actor function
int produce_item(); // Generate an item 
void put_item(int item); // Insert an item in the buffer 
int get_item(); // Remove an item from the buffer
void consume_item(int item); // Consume (print) the item passed as argument
void adjust_production_rate(double queueUsage); // Adjust the production rate
void fineAdjust_production_rate(double queueUsage); // Fine adjust the production rate (working only with buffer size >= 50)
void pauseAdjust_production_rate(double queueUsage); // Adjust the production rate with thresholds to pause and resume the production
void clean_resources(); // Clean up resources to terminate safely
void signal_handler(int signum); // Signal handler (for termination message)

// Utility function prototypes
void printQueue(); // Print the queue content
void* userInputListener(void *arg); // Function to remain in listen to user inputs
void* ticker_thread(void* arg); // Ticker thread to count the runtime duration 
void print_usage_guide(const char *programName); // Print how to use the program from command line
void print_parameters(); // Print the details about the runtime parameters

int main(int argc, char *argv[]) {
    // Initialize buffer size and other parameters at defaults
    bufferSize = DEFAULT_BUFFER_SIZE;
    lowerThreshold = DEFAULT_LOWER_THRESHOLD;
    upperThreshold = DEFAULT_UPPER_THRESHOLD;
    sleepTime = DEFAULT_SLEEP_TIME;
    actorSleepTime = DEFAULT_ACTOR_SLEEP_TIME;
    producerRate = DEFAULT_PRODUCER_RATE;

    // Initialize and validate buffer size and other parameters based on command line arguments or defaults if illegal parameters
    for (int i = 1; i < argc; i++) {
        // Check the command line arguments for `--h`, `--help`, or `--usage`
        if (strcmp(argv[i], "--h") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--usage") == 0) {
            print_usage_guide(argv[0]);
            return 0; // Exit after printing help information
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
        } else if (strcmp(argv[i], "-st") == 0) {
            if (i + 1 < argc) {
                sleepTime = atoi(argv[i + 1]);
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
    print_parameters();

    /*
    // Check the command line arguments for `-h`, `-help`, or `-usage`
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "-usage") == 0) {
            print_usage(argv[0]);
            return 0; // Exit after printing help information
        } 
    }

    // Initialize and validate buffer size and other parameters based on command line arguments or defaults
    bufferSize = (argc > 1) ? atoi(argv[1]) : DEFAULT_BUFFER_SIZE;
    if(bufferSize <= 0) 
        printf("Illegal buffer size: %d - Set at default: %d\n", bufferSize, DEFAULT_BUFFER_SIZE); bufferSize = DEFAULT_BUFFER_SIZE;
    lowerThreshold = (argc > 2) ? atof(argv[2]) : DEFAULT_LOWER_THRESHOLD;
    if(lowerThreshold <= 0) 
        printf("Illegal lower threshold: %.2f - Set at default: %.2f\n", lowerThreshold, DEFAULT_LOWER_THRESHOLD); lowerThreshold = DEFAULT_LOWER_THRESHOLD;
    upperThreshold = (argc > 3) ? atof(argv[3]) : DEFAULT_UPPER_THRESHOLD;
    if(upperThreshold >= 1) 
        printf("Illegal upper threshold: %.2f - Set at default: %.2f\n", upperThreshold, DEFAULT_UPPER_THRESHOLD); upperThreshold = DEFAULT_UPPER_THRESHOLD;
    sleepTime = (argc > 4) ? atoi(argv[4]) : DEFAULT_SLEEP_TIME;
    actorSleepTime = (argc > 5) ? atoi(argv[5]) : DEFAULT_ACTOR_SLEEP_TIME;
    producerRate = (argc > 6) ? atoi(argv[6]) : DEFAULT_PRODUCER_RATE;
    if(producerRate < 0)
        printf("Illegal producer rate %d - Set at default: %d\n", producerRate, DEFAULT_PRODUCER_RATE); producerRate = DEFAULT_PRODUCER_RATE;
    */

    int timeoutDuration;
    printf("Available termination methods:\n");
    printf("1) Timeout\n");
    printf("2) Wait for 'q' and Enter\n");
    printf("3) Run indefinitely until CTRL+C\n");
    
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
    buffer = malloc(bufferSize * sizeof(int));
    if(buffer == NULL) {
        perror("Unable to allocate buffer");
        return EXIT_FAILURE;
    } else {
        for (int i = 0; i < bufferSize; i++) {
            buffer[i] = 0; // Initialize the buffer to empty
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

    // Create and start the ticker thread
    if (pthread_create(&tickThread, NULL, ticker_thread, NULL) != 0) {
        perror("Failed to create ticker thread");
        return 1;
    } else {
        printf("Ticker thread created\n");
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
    printf("Semaphores and mutex created\n");
    printf("----------------------------------------------------------\n\n\n");

    // Set up signal handler to catch SIGALRM for program timeout
    signal(SIGALRM, signal_handler);
    alarm(timeoutDuration); // Schedule alarm to send SIGALRM after user specified timeout
    
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

    clean_resources();
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

            pthread_mutex_lock(&mutex); // Enter critical section
            // Check to ensure the buffer isn't full before adding
            if (count < bufferSize) {
                put_item(item); // Add one item to the buffer
                sem_post(&full); // Signal that an item was added
            }
            pthread_mutex_unlock(&mutex); // Exit critical section
        }
        
        sleep(sleepTime); // Sleep after attempting to produce `localRate` items
    }
    return NULL;
}

// Consumer function
void *consumer(void *arg) {
    sleep(3);
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

// Actor function
void *actor(void *arg) {
    sleep(3);
    while (true) {
        sleep(actorSleepTime); // Sleep for specified time
        pthread_mutex_lock(&mutex);
        // Critical section
        double queueUsage = (double)count / bufferSize;
        pthread_mutex_unlock(&mutex);

        // Adjust the production rate outside of the mutex-protected section
        pthread_mutex_lock(&producerRateMutex);
        //adjust_production_rate(queueUsage); // This function must be thread-safe
        adjust_production_rate(queueUsage);
        pthread_mutex_unlock(&producerRateMutex);
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
    consIdx++;
    return item;
}


void consume_item(int item) { // Do something with the consumed item
    printf("Consumed: %d", item);
    printQueue(); // Call to visualize the queue after consuming an item
}

void adjust_production_rate(double queueUsage) { // Adjust the production rate based on queue thresholds
    if (queueUsage < lowerThreshold && (count + producerRate) < bufferSize) {
        producerRate++; // Try to increment the production rate
        printf("Producer rate INCREMENTED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
    } else if (queueUsage > upperThreshold && producerRate >= 1) {
        producerRate--; // Try to decrement the production rate
        if(queueUsage >= 0.9 && producerRate > 0) {
            producerRate--; // Try to decrement the production rate to avoid overflow
            printf("Producer rate HARD DECREMENTED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
        }
        else {
            printf("Producer rate SOFT DECREMENTED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
        }
    } else {
        printf("Producer rate NOT CHANGED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
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
            producerRate += (lowerThreshold - queueUsage > lowerThreshold * 0.5) ? 1 : 2; // if queueUsage < half 
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

void pauseAdjust_production_rate(double queueUsage) {
    const double pauseThreshold = upperThreshold + (1 - upperThreshold / 2); // Buffer usage threshold to pause production

    // We need a mechanism to resume production from a paused state:
    const double resumeThreshold = upperThreshold - (1 - upperThreshold / 2); // Buffer usage threshold to resume production from pause

    const int bufferAvailable = bufferSize - count - producerRate; // Simulate another production at current rate 

    if (producerRate > 0 && queueUsage >= upperThreshold) { // If active producer and usage over upper threshold
        if (queueUsage >= pauseThreshold || bufferAvailable <= 1) { // If critical over-usage
            // Pause producer entirely to avoid overflow
            producerRate = 0;
            printf("Producer rate PAUSED, Queue usage: %0.2f - Free: %d\n", queueUsage, bufferAvailable);
        } else {
            // Decrement rate to prevent buffer overflow
            producerRate = (producerRate-1 > 0) ? producerRate-1 : 0;
            if(producerRate == 0) 
                printf("Producer rate PAUSED, Queue usage: %0.2f - Free: %d\n", queueUsage, bufferAvailable);   
            printf("Producer rate DECREMENTED to: %d - Queue usage: %0.2f - Free: %d\n", producerRate, queueUsage, bufferAvailable);
        }
    } else if (producerRate == 0 && queueUsage <= resumeThreshold) {
        // Resume production if paused and buffer usage has decreased sufficiently
        producerRate = 1; // Starting back production with low rate
        printf("Producer rate RESUMED at: %d - Queue usage: %0.2f - Free: %d\n", producerRate, queueUsage, bufferAvailable);
    } else if (producerRate > 0 && queueUsage < lowerThreshold && bufferAvailable > producerRate) {
        // Safely increment producer rate if reasonable buffer space is available
        producerRate++; 
        printf("Producer rate INCREMENTED to: %d - Queue usage: %0.2f - Free: %d\n", producerRate, queueUsage, bufferAvailable);
    } else {
        printf("Producer rate NOT CHANGED to: %d - Queue usage: %0.2f - Free: %d\n", producerRate, queueUsage, bufferAvailable);
    }
}

// Clean up resources like threads, mutexes, semaphores, buffer...
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
    printf("Number of items left in the queue: %d\n", count);
    int lostItems = prodIdx-consIdx-count;
    double percLostItems = (lostItems/prodIdx)*100;
    printf("Number of loss items: %d - Percentage: %.1f%% \n", lostItems, percLostItems);
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
        if (buffer[i] == 0) {
            printf("--|");
        } else {
            printf("%2d|", buffer[i]);
        }
    }
    printf("\n");
}

// Input listener to terminate on user input 'q'
void *userInputListener(void *arg) {
    char input;
    while (1) {
        input = getchar();
        if (input == 'q') {
            terminate = 1;
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

// Print how to launch the program from command line
void print_usage_guide(const char *programName) {
    printf("Usage: %s [options]\n", programName);
    printf("Options:\n");
    printf("\t-bs <buffer size (int)>\t\t: Set the buffer size (default: %d)\n", DEFAULT_BUFFER_SIZE);
    printf("\t-lt <lower threshold (double)>\t: Set the lower threshold (default: %.2f)\n", DEFAULT_LOWER_THRESHOLD);
    printf("\t-ut <upper threshold (double)>\t: Set the upper threshold (default: %.2f)\n", DEFAULT_UPPER_THRESHOLD);
    printf("\t-st <sleep time (int)>\t\t: Set the sleep time in seconds (default: %d)\n", DEFAULT_SLEEP_TIME);
    printf("\t-ast <actor sleep time (int)>\t: Set the actor sleep time in seconds (default: %d)\n", DEFAULT_ACTOR_SLEEP_TIME);
    printf("\t-pr <producer rate (int)>\t: Set the producer rate (default: %d)\n", DEFAULT_PRODUCER_RATE);
    printf("Example:\n");
    printf("\t%s -bs 50 -lt 0.3 -ut 0.7 -st 2 -ast 2 -pr 3\n", programName);
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
    printf("  sleepTime           %d ", sleepTime);
    (sleepTime == DEFAULT_SLEEP_TIME) ? printf("\t[default]\n") : printf("\n");
    printf("  actorSleepTime      %d ", actorSleepTime);
    (actorSleepTime == DEFAULT_ACTOR_SLEEP_TIME) ? printf("\t[default]\n") : printf("\n");
    printf("  producerRate        %d ", producerRate);
    (producerRate == DEFAULT_PRODUCER_RATE) ? printf("\t[default]\n") : printf("\n");
    printf("----------------------------------------------------------\n\n");
}
