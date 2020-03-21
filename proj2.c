#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <stdbool.h>
#include <string.h>

#define ERROR(msg)                    \
    {                                 \
        fprintf(stderr, "%s\n", msg); \
        cleanMemory();                \
        return 1;                     \
    }

// macro for incrementing a semaphore by n
#define SIGNAL(sem, n)              \
    {                               \
        for (int i = 0; i < n; i++) \
            sem_post(sem);          \
    }

// structure used to store arguments for convenience
typedef struct
{
    int numberOfPersons;
    int hackersInterval;
    int serfsInterval;
    int cruiseTime;
    int pierReturnTime;
    int pierCapacity;
} Arguments;

// structure used to store information that will be shared between processes (shared memory)
typedef struct
{
    int actionCounter;
    int hackerPierCount;
    int serfPierCount;
    int exitCounter;
    int onBoard;
} SharedVariables;

enum errorCodes
{
    OK,
    ARG_COUNT_ERR,
    ARG_UNEXPECTED_ERR,
    ARG_RANGE_ERR,
    FORK_ERR
};

#define SEM_SIZE sizeof(sem_t)
#define SV_SIZE sizeof(SharedVariables)

Arguments arguments;

FILE *fp;
SharedVariables *sharedVariables;

// mutex to ensure that only one process writes to a file or updates shared memory at a time
sem_t *fileSem;

sem_t *hackerQueue;
sem_t *serfQueue;
sem_t *sailingDone;
sem_t *captainLast;
sem_t *mutex;
sem_t *boardingDone;

// function used to destroy and unmap used semaphores from memory, close opened file
void cleanMemory()
{
    sem_destroy(fileSem);
    munmap(fileSem, SEM_SIZE);

    sem_destroy(hackerQueue);
    munmap(hackerQueue, SEM_SIZE);

    sem_destroy(serfQueue);
    munmap(serfQueue, SEM_SIZE);

    sem_destroy(sailingDone);
    munmap(sailingDone, SEM_SIZE);

    sem_destroy(captainLast);
    munmap(captainLast, SEM_SIZE);

    sem_destroy(mutex);
    munmap(mutex, SEM_SIZE);

    sem_destroy(boardingDone);
    munmap(boardingDone, SEM_SIZE);

    munmap(sharedVariables, sizeof(SharedVariables));

    fclose(fp);
}

// function used to process arguments
int processArguments(int argc, char *argv[], Arguments *arguments)
{
    // too many or not enough arguments error
    if (argc != 7)
        return ARG_COUNT_ERR;

    char *end;

    arguments->numberOfPersons = strtoul(argv[1], &end, 10);
    if (*end)
        return ARG_UNEXPECTED_ERR;
    if (arguments->numberOfPersons < 2 || arguments->numberOfPersons % 2 != 0)
        return ARG_RANGE_ERR;

    // check if every argument matches the correct format and is not out of allowed range

    arguments->hackersInterval = strtoul(argv[2], &end, 10);
    if (*end)
        return ARG_UNEXPECTED_ERR;
    if (arguments->hackersInterval < 0 || arguments->hackersInterval > 2000)
        return ARG_RANGE_ERR;

    arguments->serfsInterval = strtoul(argv[3], &end, 10);
    if (*end)
        return ARG_UNEXPECTED_ERR;
    if (arguments->serfsInterval < 0 || arguments->serfsInterval > 2000)
        return ARG_RANGE_ERR;

    arguments->cruiseTime = strtoul(argv[4], &end, 10);
    if (*end)
        return ARG_UNEXPECTED_ERR;
    if (arguments->cruiseTime < 0 || arguments->cruiseTime > 2000)
        return ARG_RANGE_ERR;

    arguments->pierReturnTime = strtoul(argv[5], &end, 10);
    if (*end)
        return ARG_UNEXPECTED_ERR;
    if (arguments->pierReturnTime < 20 || arguments->pierReturnTime > 2000)
        return ARG_RANGE_ERR;

    arguments->pierCapacity = strtoul(argv[6], &end, 10);
    if (*end)
        return ARG_UNEXPECTED_ERR;
    if (arguments->pierCapacity < 5)
        return ARG_RANGE_ERR;

    return 0;
}

// function used to print actions of processes
void printAction(char *name, char *action, int *counter)
{
    // "starts" and "is back" actions have different format - no counter of hackers/serfs waiting on pier
    if (strcmp(action, "starts") == 0 || strcmp(action, "is back") == 0)
    {
        fprintf(fp, "%d  : %s %d     : %s\n", sharedVariables->actionCounter, name, *counter, action);
    }
    else
    {
        fprintf(fp, "%d  : %s %d     : %s        : %d        : %d\n", sharedVariables->actionCounter, name, *counter, action, sharedVariables->hackerPierCount, sharedVariables->serfPierCount);
    }
    sharedVariables->actionCounter++;
}

// function for hackers or serfs can try to join the pier
void joinPear(char *name, int *counter, int *pierCounter, int *otherPierCounter)
{
    bool isCaptain = false;
    sem_t *queue, *otherQueue;

    // determine which queue is the "main" queue
    if (strcmp(name, "HACK") == 0)
    {
        queue = hackerQueue;
        otherQueue = serfQueue;
    }
    else
    {
        queue = serfQueue;
        otherQueue = hackerQueue;
    }

    sem_wait(fileSem);

    // check if the pier is full
    if (sharedVariables->hackerPierCount + sharedVariables->serfPierCount == arguments.pierCapacity)
    {
        // sem_wait(fileSem);
        printAction(name, "leaves queue", counter);
        sem_post(fileSem);

        // sleep process for miliseconds in range specified by an argument (W) - return to pier after n miliseconds
        int random = rand() % arguments.pierReturnTime;
        usleep(random * 1000);

        sem_wait(fileSem);
        printAction(name, "is back", counter);
        sem_post(fileSem);

        // after sleeping, the process tries to join the pear once again (until it successfully joins)
        joinPear(name, counter, pierCounter, otherPierCounter);
    }
    else
    {
        // the pier is not full

        sem_post(fileSem);

        sem_wait(mutex);

        sem_wait(fileSem);
        (*pierCounter)++;
        printAction(name, "waits", counter);
        sem_post(fileSem);

        // check if processes on pier are able create a suitable group
        // either 4 processes of the same type (hacker/serf)
        if (*pierCounter == 4)
        {
            // allow 4 processes to board and decrement pierCounter because they are no more on the pear
            SIGNAL(queue, 4);
            *pierCounter = 0;

            isCaptain = true;
        }
        // or 2 processes of each type (hacker/serf)
        else if (*pierCounter == 2 && *otherPierCounter >= 2)
        {
            // allow 4 processes to board and decrement their counters because they are no more on the pear
            SIGNAL(queue, 2);
            SIGNAL(otherQueue, 2);
            *pierCounter = 0;
            *otherPierCounter -= 2;

            isCaptain = true;
        }
        else
        {
            sem_post(mutex);
        }

        // wait in queue to board
        sem_wait(queue);

        if (isCaptain)
        {
            sem_wait(fileSem);
            printAction(name, "boards", counter);
            sem_post(fileSem);

            // wait for other processes to board
            sem_wait(boardingDone);

            if (arguments.cruiseTime != 0)
            {
                // sleep process for miliseconds in range specified by an argument (R) - duration of the cruise
                int random = rand() % arguments.cruiseTime;
                usleep(random * 1000);
            }

            // let other processes know that the cruise has ended
            SIGNAL(sailingDone, 3);

            // wait for other processes to exit the boat - the captain has to exit last
            sem_wait(captainLast);

            sem_wait(fileSem);
            printAction(name, "captain exits", counter);
            sharedVariables->exitCounter = 0;
            sharedVariables->onBoard = 0;
            sem_post(fileSem);

            sem_post(mutex);
        }
        else
        {
            // is not captain

            sem_wait(fileSem);
            sharedVariables->onBoard++;
            // let captain know that everyone boarded
            if (sharedVariables->onBoard == 3)
            {
                sem_post(boardingDone);
            }
            sem_post(fileSem);

            // wait for the cruise to end
            sem_wait(sailingDone);

            sem_wait(fileSem);
            printAction(name, "member exits", counter);
            sharedVariables->exitCounter++;
            // let captain know that everyone has exited the boat
            if (sharedVariables->exitCounter == 3)
                sem_post(captainLast);
            sem_post(fileSem);
        }
    }
}

// function used to generate hacker and serf processes
int generate(int interval, int *counter, int *pierCounter, int *otherPierCounter, char *name)
{
    pid_t pid;

    // generate p processes
    for (int i = 0; i < arguments.numberOfPersons; i++)
    {
        if (interval != 0)
        {
            // sleep process for miliseconds in range specified by an argument (H/S) - generate hacker/serf every n miliseconds
            int random = rand() % interval;
            usleep(random * 1000);
        }

        if ((pid = fork()) < 0)
            return FORK_ERR;

        (*counter)++;

        // code for generator child
        if (pid == 0)
        {
            sem_wait(fileSem);
            printAction(name, "starts", counter);
            sem_post(fileSem);

            // try to join pear
            joinPear(name, counter, pierCounter, otherPierCounter);

            cleanMemory();

            exit(0);
        }
    }

    // wait for every child process of the generator to exit
    while (wait(NULL) > 0)
        ;

    cleanMemory();

    exit(0);

    return 0;
}

int main(int argc, char *argv[])
{
    char *errorMessages[] = {"Too many / Not enough arguments", "Unexpected argument", "Argument out of allowed range", "Fork error"};

    fp = fopen("proj2.out", "w");
    setbuf(fp, NULL);

    int returnCode = processArguments(argc, argv, &arguments);
    if (returnCode)
        ERROR(errorMessages[returnCode - 1]);

    // create shared memory to share information between processes vital for synchronization
    sharedVariables = (SharedVariables *)mmap(NULL, sizeof(SharedVariables), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sharedVariables->actionCounter = 1;
    sharedVariables->hackerPierCount = 0;
    sharedVariables->serfPierCount = 0;
    sharedVariables->exitCounter = 0;

    fileSem = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(fileSem, 1, 1);

    hackerQueue = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(hackerQueue, 1, 0);

    serfQueue = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(serfQueue, 1, 0);

    sailingDone = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(sailingDone, 1, 0);

    captainLast = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(captainLast, 1, 0);

    mutex = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(mutex, 1, 1);

    boardingDone = mmap(NULL, SEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(boardingDone, 1, 1);

    srand(time(NULL));

    pid_t hackerGenerator, serfGenerator;
    // variables used to store hacker/serf "ID"
    int hackerCount = 0;
    int serfCount = 0;

    // create process for generating hackers
    if ((hackerGenerator = fork()) < 0)
        ERROR(errorMessages[FORK_ERR]);

    if (hackerGenerator == 0)
    {
        // generate hacker processes
        returnCode = generate(arguments.hackersInterval, &hackerCount, &sharedVariables->hackerPierCount, &sharedVariables->serfPierCount, "HACK");
        if (returnCode != OK)
            ERROR(errorMessages[returnCode]);
    }

    if (hackerGenerator != 0)
    {
        // create process for generating serfs
        if ((serfGenerator = fork()) < 0)
            ERROR(errorMessages[FORK_ERR]);
    }

    if (serfGenerator == 0)
    {
        // generate serf processes
        returnCode = generate(arguments.serfsInterval, &serfCount, &sharedVariables->serfPierCount, &sharedVariables->hackerPierCount, "SERF");
        if (returnCode != OK)
            ERROR(errorMessages[returnCode]);
    }

    // wait for both generators to exit
    while (wait(NULL) > 0)
        ;

    cleanMemory();

    exit(0);

    cleanMemory();

    return 0;
}