#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmalloc/cmalloc.h>

typedef struct data_struct {
    char c;
    int n;
    long l;
} data_t;

int main() {

    printf("Mallocing data_null\n");
    data_t *data_null = cmalloc(sizeof(data_t));
    printf("    c: %c, n: %d, l: %ld\n", data_null->c, data_null->n, data_null->l);

    printf("Mallocing data, a, 10, 256\n");
    data_t *data = cmalloc(sizeof(data_t));
    printf("    Setting Data\n");
    data->c = 'a';
    data->n = 10;
    data->l = 256;

    printf("    c: %c, n: %d, l: %ld\n", data->c, data->n, data->l);

    printf("Mallocing data1, b, 20, 1\n");
    data_t *data1 = cmalloc(sizeof(data_t));
    printf("    Setting Data\n");
    data1->c = 'b';
    data1->n = 20;
    data1->l = 1;

    printf("    c: %c, n: %d, l: %ld\n", data1->c, data1->n, data1->l);
    printf("data:    c: %c, n: %d, l: %ld\n", data->c, data->n, data->l);

    // printf("Freeing data\n");
    // cfree(data);
    // data = NULL;
    printf("Freeing data1\n");
    cfree(data1);
    data1 = NULL;

    printf("Mallocing\n");
    data_t *data2 = cmalloc(sizeof(data_t));
    printf("    c: %c, n: %d, l: %ld\n", data2->c, data2->n, data2->l);

    printf("\nSmall allocing 32768x\n");
    for (int i = 0; i < 32768; i++) {
        void *ptr = cmalloc(i);
        cfree(ptr);
    }
    printf("Success\n\n");


    printf("Large Malloc\n");
    void *large_alloc = cmalloc(65536);
    *((int *) large_alloc) = 10;
    *((char *)((char *) large_alloc + 4)) = 'b';
    *((long *)((char *) large_alloc + 8)) = 256;
    *((char *)((char *) large_alloc + 65536 - 1)) = 'a';
    printf("    byte 0: %d, byte 4: %c, byte 8: %ld, byte 65535: %c\n", 
        *((int *) large_alloc),
        *((char *)((char *) large_alloc + 4)),
        *((long *)((char *) large_alloc + 8)),
        *((char *)((char *) large_alloc + 65536 - 1)));
    printf("Freeing large malloc\n");
    cfree(large_alloc);

    printf("Super big malloc:\n");
    large_alloc = cmalloc(65536 * 100);
    cfree(large_alloc);
    printf("Success:\n");

    printf("\nMallocing 100 times:\n");
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = cmalloc(65536 * i);
        void *ptr = ptrs[i];
        assert(ptr);
        *((int *) ptr) = 10;
        *((char *)((char *) ptr + 4)) = 'b';
        *((long *)((char *) ptr + 8)) = 256;
        *((char *)((char *) ptr + 65536 - 1)) = 'a';
    }
    for (int i = 0 ; i < 100; i++) {
        cfree(ptrs[i]);
    }
    printf("Success\n\n");

    return 0;
}