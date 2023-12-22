#include <stdlib.h>
#include <stdio.h>

typedef struct entry {
    char c;
    long long int n;
} entry_t;


typedef entry_t* table_row_t;
typedef table_row_t* table_t;


table_t table;

void print_table(int n_rows, int n_cols) {
    for (int i = 0; i < n_rows; i++) {
        for (int j = 0; j < n_cols; j++) {
            printf("(%c,%lld) ", table[i][j].c, table[i][j].n); 
        }
        printf("\n");
    }
}

void read_file(const char *filename, int n_rows, int n_cols) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    char line[256];
    long long int i, j, m;
    char d;

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "(%lld,%lld):(%c,%lld)", &i, &j, &d, &m) == 4) {
            if (i >= 0 && i < n_rows && j >= 0 && j < n_cols) {
                table[i][j].c = d;
                table[i][j].n = m;
            }
        }
    }
    fclose(file);
}

void determine_table_size(const char *filename, int *max_rows, int *max_cols) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    char line[256];
    long long int i, j, m;
    char d;
    *max_rows = 0;
    *max_cols = 0;

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "(%lld,%lld):(%c,%lld)", &i, &j, &d, &m) == 4) {
            if (i > *max_rows) *max_rows = i;
            if (j > *max_cols) *max_cols = j;
        }
    }
    fclose(file);

    
    (*max_rows)++;
    (*max_cols)++;
}

int main() {
    int n_rows;
    int n_cols;

    determine_table_size("data.txt", &n_rows, &n_cols);

    table = (table_t) calloc(n_rows, sizeof(table_row_t));
    for (int i = 0; i < n_rows; i++) {
        table[i] = (table_row_t) calloc(n_cols, sizeof(entry_t));
    }

    read_file("data.txt", n_rows, n_cols);
    print_table(n_rows, n_cols);

    
   for (int i = 0; i < n_rows; i++) {
    free(table[i]);
}

    free(table);

    return 0;
}
