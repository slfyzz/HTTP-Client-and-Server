#include <stdio.h>
#include <vector>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>

#include <pthread.h>
#include <mutex>
#include <semaphore.h>

#include <queue>
#include <map>

const int DEFAULT_TIME_OUT = 60 * 1000;
const int MIN_TIME_OUT = 10 * 1000;

const int MAX_THREADS = 20;

std::map<int, pthread_t*> sockfdToThread;
std::queue<pthread_t*> availableThread;
pthread_t threads[MAX_THREADS];


std::mutex connMutex;
sem_t semaphore;


void resetThreads() {
    // there's a problem with freeing the thread, which is that we call freeThread inside the thread itself. 
    // at the end, so if the system is really busy that could lead to a problem. 
    // tricky solution is just allow MAX_THREADS - 2, so that there's always 2 free threads ready for this scenarios.
    // maybe it's needed to find a more robust way to handle it. 
    sem_init(&semaphore, 0, MAX_THREADS - 2);
    connMutex.lock();
    for (int i = 0; i < MAX_THREADS; i++) {
        availableThread.push(&threads[i]);
    } 
    connMutex.unlock();
}

void freeThread (int fd) {
    std::cout << "Freeing the thread with ID: " << fd << '\n';
    connMutex.lock();
    if (sockfdToThread.count(fd)) {
        pthread_t* t = sockfdToThread.at(fd);
        if (t != NULL) { 
            sockfdToThread[fd] = NULL;
            availableThread.emplace(t);
            std::cout << "Done Freeing the thread with ID: " << fd << '\n';
            sem_post(&semaphore);
        } 
    }
    connMutex.unlock();
}

pthread_t* getAvailableThread(int fd) {
    std::cout << "Trying to get a thread from the pool.\n";
    pthread_t* ptr;
    sem_wait(&semaphore);
    connMutex.lock();
    ptr = availableThread.front();
    pthread_join(*ptr, NULL);
    availableThread.pop();
    sockfdToThread[fd] = ptr;
    std::cout << "Got a thread with ID: " << fd << '\n';
    std::cout << "Remaining in the pool is " << availableThread.size() << '\n';
    connMutex.unlock();
    return ptr;
}


int estimateTimeOut() {
    connMutex.lock();
    int time = std::max(MIN_TIME_OUT, (int)(DEFAULT_TIME_OUT  / std::max(1, MAX_THREADS - (int)availableThread.size())));
    connMutex.unlock();
    return time;
}