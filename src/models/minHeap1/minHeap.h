#pragma once
#include "stdlib.h"
#include "stdio.h"
#include <stdbool.h>


typedef struct MinHeap
{
    void **array;
    int capacity;
    int size;
    int (*compare)(void *a, void *b);
} MinHeap;



MinHeap *createMinHeap(int capacity, int (*compare)(void *a, void *b));

void insertMinHeap(MinHeap *min_h, void *data);
int getElementIndex(MinHeap *min_h, void *data);
void *extractMin (MinHeap *min_h);
void *getMin (MinHeap*min_h);
void minHeapify(MinHeap *min_h, int ind);
void printHeap(MinHeap*min_h);
void destroyHeap(MinHeap*min_h);
