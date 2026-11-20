/* linkq.h — a lock-free single-producer, single-consumer queue of small events (MIDI messages and parameter
 * values), for the network core of a board to hand events to its audio core through shared memory (on the
 * H745: the M4 pushes from its Ethernet interrupt, the M7 pops at the start of each audio buffer). Each index
 * has one writer; a barrier orders the event before the index that publishes it (Lamport's queue). */
#ifndef LINKQ_H
#define LINKQ_H

#include <stdint.h>

#define LINKQ_LEN 256u /* power of two */

typedef enum { LINKQ_MIDI = 1, LINKQ_PARAM = 2 } linkq_kind_t;

typedef struct {
    uint8_t kind; /* linkq_kind_t */
    uint8_t len;  /* MIDI: 1 to 3 bytes */
    uint8_t midi[3];
    uint8_t pad;
    uint16_t id;  /* PARAM: synth_set_param id */
    float value;  /* PARAM: 0..1 */
} linkq_event_t;

typedef struct {
    volatile uint32_t head; /* written by the producer: events pushed */
    volatile uint32_t tail; /* written by the consumer: events popped */
    volatile uint32_t lost; /* written by the producer: events dropped because the queue was full */
    uint32_t reserved;
    linkq_event_t ev[LINKQ_LEN];
} linkq_t;

static inline void linkq_barrier(void) { __sync_synchronize(); }

/* Both indexes to 0 (before either core uses the queue). */
static inline void linkq_init(linkq_t *q)
{
    q->head = q->tail = q->lost = 0;
}

/* Producer: returns 1, or 0 if the queue is full (the event is counted in lost). */
static inline int linkq_push(linkq_t *q, const linkq_event_t *e)
{
    const uint32_t h = q->head;
    if (h - q->tail >= LINKQ_LEN) {
        q->lost++;
        return 0;
    }
    q->ev[h & (LINKQ_LEN - 1u)] = *e;
    linkq_barrier(); /* the event before the index that publishes it */
    q->head = h + 1u;
    return 1;
}

/* Consumer: returns 1 with the oldest event in e, or 0 if the queue is empty. */
static inline int linkq_pop(linkq_t *q, linkq_event_t *e)
{
    const uint32_t t = q->tail;
    if (q->head == t)
        return 0;
    linkq_barrier(); /* the index before the event it published */
    *e = q->ev[t & (LINKQ_LEN - 1u)];
    linkq_barrier(); /* the event read before its slot is given back */
    q->tail = t + 1u;
    return 1;
}

#endif /* LINKQ_H */
