/**
 * @file   queue.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Singly linked list.
 * 
 */

static inline qanchor_t **next_field(qanchor_t *qanc, queue_t *queue){    
    return (qanc == NULL) ? &queue->head : &qanc->next;
}

void queue_push(qanchor_t *qanc, queue_t *queue){
    *next_field(queue->tail, queues) = qanc;
    queue->tail = qanc;
}

qanchor_t *queue_pop(queue_t *queue){
    qanc *next = queue->head->next;
    queue->head = next;
    if(!next)
        queue->tail = NULL;
}

void queue_splice(qanchor_t *queue_head, queue_t *queue){
    queue_push(queue_head, queue);
}
