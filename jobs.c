// ------------
// This code is provided solely for the personal and private use of
// students taking the CSC369H5 course at the University of Toronto.
// Copying for purposes other than this use is expressly prohibited.
// All forms of distribution of this code, whether as given or with
// any changes, are expressly prohibited.
//
// Authors: Bogdan Simion
//
// All of the files in this directory and all subdirectories are:
// Copyright (c) 2019 Bogdan Simion
// -------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "executor.h"

extern struct executor tassadar;


/**
 * Populate the job lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <type> <num_resources> <resource_id_0> <resource_id_1> ...
 *
 * Each job is added to the queue that corresponds with its job type.
 */
void parse_jobs(char *file_name) {
    int id;
    struct job *cur_job;
    struct admission_queue *cur_queue;
    enum job_type jtype;
    int num_resources, i;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*) &jtype, (int*) &num_resources) == 3) {

        /* construct job */
        cur_job = malloc(sizeof(struct job));
        cur_job->id = id;
        cur_job->type = jtype;
        cur_job->num_resources = num_resources;
        cur_job->resources = malloc(num_resources * sizeof(int));

        int resource_id; 
				for(i = 0; i < num_resources; i++) {
				    fscanf(f, "%d ", &resource_id);
				    cur_job->resources[i] = resource_id;
				    tassadar.resource_utilization_check[resource_id]++;
				}
				
				assign_processor(cur_job);

        /* append new job to head of corresponding list */
        cur_queue = &tassadar.admission_queues[jtype];
        cur_job->next = cur_queue->pending_jobs;
        cur_queue->pending_jobs = cur_job;
        cur_queue->pending_admission++;
    }

    fclose(f);
}

/*
 * Magic algorithm to assign a processor to a job.
 */
void assign_processor(struct job* job) {
    int i, proc = job->resources[0];
    for(i = 1; i < job->num_resources; i++) {
        if(proc < job->resources[i]) {
            proc = job->resources[i];
        }
    }
    job->processor = proc % NUM_PROCESSORS;
}


void do_stuff(struct job *job) {
    /* Job prints its id, its type, and its assigned processor */
    printf("%d %d %d\n", job->id, job->type, job->processor);
}



/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the executor
 * before any jobs start coming
 * 
 */
void init_executor() {
    //Initialize recources
    for (int i = 0; i < NUM_RESOURCES; i++)
    {
        //Initialize Lock for every recource
        pthread_mutex_init(&tassadar.resource_locks[i], NULL);
        //Set initial values
        tassadar.resource_utilization_check[i] = 0;    
    }

    //Initialize the queues
    for (int i = 0; i < NUM_QUEUES; i++)
    {
        //Initialize locks and condition variables
        pthread_mutex_init(&tassadar.admission_queues[i].lock, NULL);
        pthread_cond_init(&tassadar.admission_queues[i].admission_cv, NULL);
        pthread_cond_init(&tassadar.admission_queues[i].execution_cv, NULL);
        //Set initial values
        tassadar.admission_queues[i].capacity = QUEUE_LENGTH;
        tassadar.admission_queues[i].head = 0;
        tassadar.admission_queues[i].tail = 0;
        tassadar.admission_queues[i].pending_admission = 0;
        tassadar.admission_queues[i].pending_jobs = malloc(sizeof(struct job) * QUEUE_LENGTH);
        tassadar.admission_queues[i].num_admitted = 0;
        tassadar.admission_queues[i].admitted_jobs = malloc(sizeof(struct job *) * QUEUE_LENGTH);      
    }

    //Initialize the processors
    for (int i =0; i < NUM_PROCESSORS; i++)
    {
        //Initialize locks
        pthread_mutex_init(&tassadar.processor_records[i].lock, NULL);
        //Set initial values
        tassadar.processor_records[i].completed_jobs = NULL;
        tassadar.processor_records[i].num_completed = 0;
    }
}


/**
 * TODO: Fill in this function
 *
 * Handles an admission queue passed in through the arg (see the executor.c file). 
 * Bring jobs into this admission queue as room becomes available in it. 
 * As new jobs are added to this admission queue (and are therefore ready to be taken
 * for execution), the corresponding execute thread must become aware of this.
 * 
 */
void *admit_jobs(void *arg) {
    struct admission_queue *q = arg;

    // This is just so that the code compiles without warnings
    // Remove this line once you implement this function
   
    //Run until no job left to be admited
    while(1)
    {
        pthread_mutex_lock(&q->lock);
        if(q->pending_admission == 0)
        {
            pthread_mutex_unlock(&q->lock);
            return NULL;
        }
        //Wait until room is available
        while(q->capacity == 0)
        {
            pthread_cond_wait(&q->admission_cv, &q->lock);
        }
        //Admit jobs
        q->admitted_jobs[q->tail] = q->pending_jobs;
        q->pending_jobs = q->pending_jobs->next;
        //Update the value of tail and number of admited jobs
        q->tail = (q->tail + 1) % QUEUE_LENGTH;
        q->num_admitted += 1;
        //Update the number of jobs left and capacity
        q->capacity -= 1;
        q->pending_admission -= 1;

        pthread_cond_signal(&q->admission_cv);
        pthread_mutex_unlock(&q->lock);
    }
    return NULL;
}


/**
 * TODO: Fill in this function
 *
 * Moves jobs from a single admission queue of the executor. 
 * Jobs must acquire the required resource locks before being able to execute. 
 *
 * Note: You do not need to spawn any new threads in here to simulate the processors.
 * When a job acquires all its required resources, it will execute do_stuff.
 * When do_stuff is finished, the job is considered to have completed.
 *
 * Once a job has completed, the admission thread must be notified since room
 * just became available in the queue. Be careful to record the job's completion
 * on its assigned processor and keep track of resources utilized. 
 *
 * Note: No printf statements are allowed in your final jobs.c code, 
 * other than the one from do_stuff!
 */
void *execute_jobs(void *arg) {
    struct admission_queue *q = arg;

   //Run until no jobs left to be executed
   while(1)
   {
       pthread_mutex_lock(&q->lock);
       //No jobs left
       if (q->pending_jobs == NULL && q->num_admitted == 0)
       {
           pthread_mutex_unlock(&q->lock);
           return NULL;
       }
        // Wait if no jobs in the admission queue
        while(q->num_admitted  == 0 ){
            pthread_cond_wait(&q->admission_cv,&q->lock);
        }
        struct job *cur = q->admitted_jobs[q->head];
        q->head  = (q->head + 1) % QUEUE_LENGTH;
        q->num_admitted -= 1;
        q->capacity++;
        // Signal the admit jobs
        pthread_cond_signal(&q->admission_cv);
        for(int i = 0; i < cur->num_resources; i ++)
        {
            //Acquire resources
            pthread_mutex_lock(&(tassadar.resource_locks[cur->resources[i]]));
            tassadar.resource_utilization_check[cur->resources[i]]--;
        }
        //Call magic function
        assign_processor(cur);
        do_stuff(cur);

        pthread_mutex_lock(&tassadar.processor_records[cur->processor].lock);
        cur->next = tassadar.processor_records[cur->processor].completed_jobs;
        tassadar.processor_records[cur->processor].completed_jobs = cur;
        tassadar.processor_records[cur->processor].num_completed++;

        pthread_mutex_unlock(&tassadar.processor_records[cur->processor].lock);
        for (int i = 0; i < cur->num_resources; i++)
        {
            pthread_mutex_unlock(&(tassadar.resource_locks[cur->resources[i]]));
        }
   }
   pthread_cond_signal(&q->execution_cv);
   pthread_mutex_unlock(&q->lock);
   return NULL;
}
