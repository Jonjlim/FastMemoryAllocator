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

    // fprint("Callocing\n");
    // data_t *datas = ccalloc(2, sizeof(data));

    // fprint("    c: %c, n: %d, l: %ld\n", datas[0].c, datas[0].n, datas[0].l);
    // fprint("    c: %c, n: %d, l: %ld\n", datas[1].c, datas[1].n, datas[1].l);

    // fprint("Reallocing\n");
    // datas = crealloc(datas, sizeof(data) * 3);
    // fprint("    c: %c, n: %d, l: %ld\n", datas[0].c, datas[0].n, datas[0].l);
    // fprint("    c: %c, n: %d, l: %ld\n", datas[1].c, datas[1].n, datas[1].l);
    // fprint("    c: %c, n: %d, l: %ld\n", datas[2].c, datas[2].n, datas[2].l);

    return 0;
}