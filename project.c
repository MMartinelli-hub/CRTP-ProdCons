#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <features.h>
#include <stdatomic.h>

// Default configuration constants
#define DEFAULT_BUFFER_SIZE 25
#define DEFAULT_LOWER_THRESHOLD 0.70
#define DEFAULT_UPPER_THRESHOLD 0.75
#define DEFAULT_SLEEP_TIME 1
#define DEFAULT_ACTOR_SLEEP_TIME 3
#define DEFAULT_PRODUCER_RATE 1

// Structure to represent a message with timestamp
typedef struct {
    int item;
    time_t timestamp;   
} Message;

// Global variables
Message *buffer; // Circular shared buffer
int bufferSize; // Size of the shared buffer
double lowerThreshold, upperThreshold; // Thresholds for the buffer usage
atomic_int producerSleepTime, consumerSleepTime, actorSleepTime; // Sleep time for the producer, consumer, and actor
int producerRate; // Number of messages the producer try to add in a single iteration before going to sleep
int in = 0, out = 0, count = 0; // Indexes for the circular buffer 

// Utility global variables
int prodIdx = 0; // Counter for the total number of produced messages
int consIdx = 0; // Counter for the total number of consumed messages 
double totDelay = 0; // Sum of the delays of the consumed messages
double minDelay = 0; // Min delay among the consumed messages
double maxDelay = 0; // Max delay among the consumed messages
int consumedMessagesForTermination = 0; // Number of messages to be consumed before terminating (optional) 
int producedMessagesForTermination = 0; // Number of messages to be produced before terminating (optional)
volatile int tickCount = 0; // Tick count variable
atomic_bool terminate; // Flag for program termination
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
int produce_message(); // Generate a message 
void put_message(int message); // Insert a message in the buffer 
Message get_message(); // Remove a message from the buffer
void consume_message(Message m); // Consume (print) the message passed as argument
void adjust_production_rate(double queueUsage); // Adjust the production rate
void clean_resources(); // Clean up resources to terminate safely
void signal_handler(int signum); // Signal handler for termination message upon specified timeout

// Utility function prototypes
void printQueue(); // Print the queue content
void printOccupationAtTime(); // Print the queue occupation and production rate 
void* ticker_thread(void* arg); // Ticker thread to count the runtime duration 
void* userInputListener(void *arg); // Function to remain in listen to user inputs

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
    printf("\t-pr  <producer rate (int)>\t: Set the initial producer rate (default: %d)\n", DEFAULT_PRODUCER_RATE);
    printf("Example:\n");
    printf("\t%s -bs 50 -lt 0.3 -ut 0.7 -pst 2 -cst 1 -ast 2 -pr 3\n", programName);
}

// Print the runtime parameters 
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
    atomic_store(&terminate, false);

    // Initialize buffer size and other parameters at defaults
    bufferSize = DEFAULT_BUFFER_SIZE;
    lowerThreshold = DEFAULT_LOWER_THRESHOLD;
    upperThreshold = DEFAULT_UPPER_THRESHOLD;
    atomic_init(&producerSleepTime, DEFAULT_SLEEP_TIME);
    atomic_init(&consumerSleepTime, DEFAULT_SLEEP_TIME);
    atomic_init(&actorSleepTime, DEFAULT_ACTOR_SLEEP_TIME);
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

    // If only the upper threshold has been defined by the user, adjust the lower one accordingly
    if(upperThreshold != DEFAULT_UPPER_THRESHOLD && lowerThreshold == DEFAULT_UPPER_THRESHOLD) {
        lowerThreshold = upperThreshold - 0.1;
        if(lowerThreshold < 0)
            lowerThreshold = 0;
    }

    print_parameters(); // print the execution parameters

    // Ask the user for the termination method 
    int terminationMethod, timeoutDuration;
    printf("Available termination methods:\n");
    printf("1) Timeout\n");
    printf("2) Wait for X consumed messages\n");
    printf("3) Wait for X produced messages\n");
    printf("4) Wait for 'q' and Enter\n");
    printf("5) Run indefinitely until CTRL+C :)\n");

    printf("\nEnter desired termination method: "); // Ask user for termination method
    scanf("%d", &terminationMethod);
    switch (terminationMethod)
    {
    case 1: // Termination with timeout
        printf("\nEnter program timeout duration in seconds (enter 0 to run indefinitely): "); // Ask user for program timeout duration
        scanf("%d", &timeoutDuration);
        if(timeoutDuration < 0)
            timeoutDuration = 0;
        alarm(timeoutDuration);
        break;
    case 2: // Termination with num of consumed messages
        printf("\nEnter number of messages to be consumed for termination: "); // Ask user for program timeout duration
        scanf("%d", &consumedMessagesForTermination);
        timeoutDuration = 0;
        alarm(timeoutDuration);
        break;
    case 3: // Termination with num of produced messages
        printf("\nEnter number of messages to be produced for termination: "); // Ask user for program timeout duration
        scanf("%d", &producedMessagesForTermination);
        timeoutDuration = 0;
        alarm(timeoutDuration);
        break;
    default:
        timeoutDuration = 0;
        alarm(timeoutDuration);
        break;
    }
    
    // Timeout before starting the actual execution
    printf("\n");
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
    printf("Semaphores and mutexes succesfully created\n");
    printf("----------------------------------------------------------\n\n\n");


    // Set up signal handler to catch SIGALRM for program timeout
    signal(SIGALRM, signal_handler);
    alarm(timeoutDuration); // Schedule alarm to send SIGALRM after user specified timeout

    // Waiting with while loop for termination
    while (!atomic_load(&terminate)) {
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
            // Set the item content for the next message, the timestamp is set by the put function
            int item = ++prodIdx; 
            sem_wait(&empty); // Decrements the empty semaphore

            // Critical Section
            pthread_mutex_lock(&mutex); // Enter critical section
            put_message(item); // Add one message to the buffer
            sem_post(&full); // Signal that a message was added
            // Terminate program if number of messages to be consumed has been defined and reached
            if(producedMessagesForTermination != 0 && prodIdx >= producedMessagesForTermination) 
                atomic_store(&terminate, true);
            
            pthread_mutex_unlock(&mutex); // Exit critical section
        }
        // Sleep for producerSleepTime after attempting to produce `localRate` messages
        sleep(atomic_load(&producerSleepTime)); 
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
        Message m = get_message(); // Retrieve one message from the buffer
        consume_message(m); // Consume the retrieved message
        sem_post(&empty); // Increments the empty semaphore

        // Terminate program if number of messages to be consumed has been defined and reached
        if(consumedMessagesForTermination != 0 && consIdx >= consumedMessagesForTermination) 
            atomic_store(&terminate, true);
        
        pthread_mutex_unlock(&mutex); // Exit critical section
        sleep(atomic_load(&consumerSleepTime)); // Sleep for consumerSleepTime after consuming an item
    }
    return NULL;
}

// Actor function
void *actor(void *arg) {
    sleep(3);
    while (true) {
        sleep(atomic_load(&actorSleepTime)); // Sleep for actorSleepTime
        
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

// Produce a message
int produce_message() {
    return (rand() % (100 - 10)) + 10; // Generate a random item between [10, 99]
}

// Put a message with the content passed as argument in the buffer
void put_message(int item) { 
    buffer[in].item = item;
    buffer[in].timestamp = time(NULL);
    in = (in + 1) % bufferSize;
    count++;
    if(debug) {
        printf("Produced: %d", item);
        printQueue(); // Call to visualize the queue after produced a message
    }
    else {
        printOccupationAtTime();
    }
}

// Take a message from the buffer
Message get_message() { 
    Message m = buffer[out];
    bzero(&buffer[out], sizeof(Message)); // Free the position
    out = (out + 1) % bufferSize;
    count--;
    consIdx++;
    return m;
}

// Do something with the consumed message
void consume_message(Message m) { 
        double delay = difftime(time(NULL), m.timestamp); // Compute the delay for the current message
        totDelay += delay; // Update the total delay
        if(delay < minDelay) // Check if minDelay needs to be updated
            minDelay = delay;
        if(delay > maxDelay) // Check if maxDelay needs to be updated
            maxDelay = delay;
    if(debug) {
        printf("Consumed: %d", m.item);
        printQueue(); // Call to visualize the queue after consuming a message
    } else {
        printOccupationAtTime();
    }
}

// Adjust the production rate based on queue thresholds
void adjust_production_rate(double queueUsage) {
    int initialProducerRate = producerRate;
    if (queueUsage < lowerThreshold && (count + producerRate +1) <= bufferSize * upperThreshold) {
        producerRate++; // Increment the production rate
    } 
    if ((queueUsage >= upperThreshold || (count + producerRate) >= bufferSize * upperThreshold) && producerRate > 0) {
        producerRate--; // Soft Decrement the production rate
    }
    if((count + producerRate) >= bufferSize * upperThreshold && producerRate > 0) {
        producerRate--; // Hard Decrement the production rate to avoid overflow
    }    

    if(debug) {
        if(producerRate == initialProducerRate) {
            printf("Producer rate NOT CHANGED to: %d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", producerRate, count, bufferSize-count, (queueUsage*100));
            return;
        } else if(producerRate > initialProducerRate) {
            printf("Producer rate INCREMENTED: %d-->%d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", initialProducerRate, producerRate, count, bufferSize-count, (queueUsage*100));
            return;
        } else if(producerRate == initialProducerRate-1) {
            printf("Producer rate SOFT DECREMENTED: %d-->%d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", initialProducerRate, producerRate, count, bufferSize-count, (queueUsage*100));
            return;
        } else {
            printf("Producer rate HARD DECREMENTED: %d-->%d - Queue usage: %d - Free: %d - Usage ratio: %.1f%%\n", initialProducerRate, producerRate, count, bufferSize-count, (queueUsage*100));
            return;
        }
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
    printf("Number of produced messages: %d\n", prodIdx);
    printf("Number of consumed messages: %d\n", consIdx);
    printf("Average delay among the consumed messages: %.3f\n", totDelay/consIdx);
    printf("Min delay among the consumed messages: %.1f\n", minDelay);
    printf("Max delay among the consumed messages: %.1f\n", maxDelay);
    int lostItems = prodIdx-consIdx;
    double percLostItems = ((double)lostItems / prodIdx)*100;
    printf("Number of messages left in the queue: %d - Percentage: %.1f%% \n", lostItems, percLostItems);
    double prodRatio = (double)prodIdx / tickCount;
    double consRatio = (double)consIdx / tickCount;
    printf("Producer ratio over time (seconds): %.2f\n", prodRatio);
    printf("Consumer ratio over time (seconds): %.2f\n", consRatio);
    printf("----------------------------------------------------------\n\n");

    // Cancel the ticker thread
    pthread_cancel(tickThread); // Here we don't need to check since it arises error and return if the init fails
    pthread_join(tickThread, NULL);   
}

// Signal handler to update terminate flag and terminate safely
void signal_handler(int signum) {
    // TODO Safe termination for signals like SIGINT (CTRL+C)
    atomic_store(&terminate, true);
}

// Print the current state of the buffer (in 'debug' mode)
void printQueue() {
    printf("\t|");
    for (int i = 0; i < bufferSize; i++) {
        if (buffer[i].item == 0) {
            printf("---|");
        } else {
            printf("%3d|", buffer[i].item);
        }
    }
    printf("\n");
}

int printIdx = 1;
// Print the queue occupation and producer rate (in 'not-debug' mode)
void printOccupationAtTime() {
    printf("%d: %.2f %d\n", printIdx++, (double)count / bufferSize, producerRate);
}

// Ticker thread that runs every second to increment the tick count
void* ticker_thread(void* arg) {
    while (!atomic_load(&terminate)) {
        pthread_mutex_lock(&tickMutex);
        tickCount++; // Increment the tick count variable
        pthread_mutex_unlock(&tickMutex);
        sleep(1); // Wait for 1 second
    }
    return NULL;
}

// Input listener to terminate on user input 'q' and adjust runtime parameters during execution
void *userInputListener(void *arg) {
    sleep(5);
    const char *commands[] = {"q", "-bs", "-lt", "-ut", "-pst", "-cst", "-ast", "-pr"};
    char input[20];
    while (1) {
        bzero(&input, sizeof(input));
        fgets(input, sizeof(input), stdin);

        // Remove new line character from the input
        input[strcspn(input, "\n")] = 0;

        // Check if the user entered one of the commands
        int commandIndex = -1;
        for(int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            if(strcmp(input, commands[i]) == 0) {
                commandIndex = i;
                break;
            }
        }

        // Process user input based on the command
        switch (commandIndex)
        {
        case 0: // q
            pthread_mutex_lock(&tickMutex); // by taking the lock mutex we assure ticker_thread is sleeping
            atomic_store(&terminate, true);
            pthread_mutex_unlock(&tickMutex);
            
            break;
        case 1: // -bs ! Resize buffer size still not supported, this case can be used for printing runtime parameters during runtime !
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            print_parameters();
            printf("Enter integer value: ");
            fgets(input, sizeof(input), stdin);
            int inputInt;
            if (sscanf(input, "%d", &inputInt) == 1) 
                printf("Input value: %d\n\n", inputInt);
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            break;
        case 2:  // -lt
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            printf("Enter lower threshold: ");
            fgets(input, sizeof(input), stdin);
            double newLowerThreshold;
            if (sscanf(input, "%lf", &newLowerThreshold) == 1) {
                if(newLowerThreshold > 0 && newLowerThreshold <= 1) {
                    lowerThreshold = newLowerThreshold;
                    printf("Lower threshold set to: %.2f\n\n", lowerThreshold);
                    print_parameters();
                }
                else {
                    printf("Invalid lower threshold. It remains unchanged.\n\n");    
                }
            } else {
                printf("Invalid lower threshold. It remains unchanged.\n\n");
            }
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            
            break;
        case 3:  // -ut
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            printf("Enter upper threshold: ");
            fgets(input, sizeof(input), stdin);
            double newUpperThreshold;
            if (sscanf(input, "%lf", &newUpperThreshold) == 1) {
                if(newUpperThreshold > 0 && newUpperThreshold <= 1) {
                    upperThreshold = newUpperThreshold;
                    printf("Upper threshold set to: %.2f\n\n", upperThreshold);
                    print_parameters();
                }
                else {
                    printf("Invalid upper threshold. It remains unchanged.\n\n");    
                }
            } else {
                printf("Invalid upper threshold. It remains unchanged.\n\n");
            }
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            
            break;
        case 4:  // -pst
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            printf("Enter producer sleep time: ");
            fgets(input, sizeof(input), stdin);
            int newProducerSleepTime;
            if (sscanf(input, "%d", &newProducerSleepTime) == 1) {
                atomic_store(&producerSleepTime, newProducerSleepTime);
                printf("Producer sleep time set to: %d\n\n", producerSleepTime);
                print_parameters();
            }  else {
                printf("Invalid upper threshold. It remains unchanged.\n\n");
            }
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            
            break;
        case 5:  // -cst
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            printf("Enter consumer sleep time: ");
            fgets(input, sizeof(input), stdin);
            int newConsumerSleepTime;
            if (sscanf(input, "%d", &newConsumerSleepTime) == 1) {
                atomic_store(&consumerSleepTime, newConsumerSleepTime);
                printf("Consumer sleep time set to: %d\n", consumerSleepTime);
                print_parameters();
            }  else {
                printf("Invalid upper threshold. It remains unchanged.\n\n");
            }
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            
            break;
        case 6:  // -ast
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            printf("Enter actor sleep time: ");
            fgets(input, sizeof(input), stdin);
            int newActorSleepTime;
            if (sscanf(input, "%d", &newActorSleepTime) == 1) {
                atomic_store(&actorSleepTime, newActorSleepTime);
                printf("Actor sleep time set to: %d\n", actorSleepTime);
                print_parameters();
            }  else {
                printf("Invalid upper threshold. It remains unchanged.\n\n");
            }
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            
            break;
        case 7:  // -pr
            pthread_mutex_lock(&mutex);
            pthread_mutex_lock(&producerRateMutex);
            printf("Enter producer rate: ");
            fgets(input, sizeof(input), stdin);
            int newProducerRate;
            if (sscanf(input, "%d", &newProducerRate) == 1) {
                producerRate = newProducerRate;
                printf("Producer rate set to: %d\n", producerRate);
                print_parameters();
            } else {
                printf("Invalid producer rate. It remains unchanged.\n\n");
            }
            pthread_mutex_unlock(&producerRateMutex);
            pthread_mutex_unlock(&mutex);
            
            break;
        default:
            if(strlen(input) >= 1)
                printf("Invalid input: %s\n\n", input);
            
            break;
        }
    }
    return NULL;
}

// It works but if we want to implement it we need also to adjust semaphores
int resizeBuffer(int newSize) {
    // Check if the new size is greater than the current size
    if(newSize < bufferSize) {
        printf("The new buffer size must be greater than the current size\n");
        return 1;
    } else if(newSize == bufferSize) {
        printf("Buffer size already set at %d\n", bufferSize);
        return 1;
    }

    // Allocate a new buffer with the specified size
    Message *newBuffer = malloc(newSize * sizeof(Message));
    if(newBuffer == NULL) {
        // Handle allocation failure
        printf("Failed to allocate memory for the new buffer");
        return 1;   
    }

    // Copy the elements from the old buffer to the new buffer
    int i = 0;
    while(count > 0) {
        newBuffer[in] = buffer[out];

        // Update indexes
        in = (in + 1) % newSize;
        out = (out + 1) % bufferSize;

        count--;
        i++;
    }

    // Free the memory occupied by the old buffer
    free(buffer);

    // Update the global buffer pointer to point to the new buffer and adjust its size and indexes
    buffer = newBuffer;
    bufferSize = newSize;
    in = i;
    count = i;
    out = 0;
    return 0;
}
