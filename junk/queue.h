/**
 * @file   queue.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Singly linked list.
 * 
 */

#ifndef QUEUE_H
#define QUEUE_H

#include <stack.h>

typedef struct queue_t{
    qanchor_t *head;
    qanchor_t *tail;
} queue_t;

#endif
