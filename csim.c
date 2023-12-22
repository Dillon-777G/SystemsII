/***** Dillon Gaughan *****/

/***** I had most of this written before the lecture and found your comments
on code optimization helpful. I did my best to replicate them. I processed my
-vflag logic while processing cache which I thought was neat but may be standard
protocol. Inintially I had more functions and more function calls but after 
reviewing optimization in class I went back to correct my mistakes. I appreciated the
format laid out by zjs and that is the one thing I wanted to imitate. In my first 66
lines I am laying out everything that will be used in my program which again while
almost certainly standard protocol, is new to me. *****/

#include "cachelab.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>

/***** Valid bit, Tag bits, local LRU counter *****/

typedef struct {
    int valid;
    unsigned long long int tag;
    int lru;  
} cache_line;

typedef struct {
    cache_line* lines;
} cache_set;

typedef struct {
    cache_set* sets;
} cache;

typedef enum {
    LOAD = 'L',
    STORE = 'S',
    MODIFY = 'M'
} operation_t;

typedef struct {
    int hit_line;
    int empty_line;
    int LRU_line;
    int set_index;               
    unsigned long long int tag;   
} search_result;

/*****  Global Variables *****/

cache myCache;
int hit_count = 0;
int miss_count = 0;
int eviction_count = 0;
unsigned long long int LRU_counter = 0;  // global LRU counter
int s, E, b, hflag = 0, vflag = 0;
char *tracefile;

/***** Helper Functions *****/

search_result search_in_cache(cache *myCache, unsigned long long int address, int s, int b, int E);
void handle_hit(cache_set *set, int hit_line);
void handle_miss(cache_set *set, unsigned long long int tag, search_result result);

/***** Core Logic Functions *****/

void process_cache(cache *myCache, operation_t op, unsigned long long int address, int size, int s, int E, int b);
cache initialize_cache(int S, int E);
void free_cache(cache *myCache, int S, int E);

/*****  Implementation *****/

search_result search_in_cache(cache *myCache, unsigned long long int address, int s, int b, int E) {
    int S = 1 << s;
    int set_index; 
    unsigned long long int tag;  

    set_index = (address >> b) & (S - 1);
    tag = address >> (s + b);
    
    cache_set *set = &myCache->sets[set_index];
    
    search_result result = {-1, -1, 0};
    for (int i = 0; i < E; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            result.hit_line = i;
            break;
        }
        if (!set->lines[i].valid && result.empty_line == -1) {
            result.empty_line = i;
        }
        if (set->lines[i].lru < set->lines[result.LRU_line].lru) {
            result.LRU_line = i;
        }
    }

    result.set_index = set_index;
    result.tag = tag;
 
    return result;
}

/***** Result Processing Logic ******/

void handle_hit(cache_set *set, int hit_line) {
    set->lines[hit_line].lru = LRU_counter++;
    hit_count++;
}

void handle_miss(cache_set *set, unsigned long long int tag, search_result result) {
    int line_to_fill = (result.empty_line != -1) ? result.empty_line : result.LRU_line;

    if (result.empty_line == -1) {
        eviction_count++;
    }

    set->lines[line_to_fill].valid = 1;
    set->lines[line_to_fill].tag = tag;
    set->lines[line_to_fill].lru = LRU_counter++;
    miss_count++;
}

/***** Process optional vflag output as well as mandatory handle_hit and handle_miss *****/

void process_cache(cache *myCache, operation_t op, unsigned long long int address, int size, int s, int E, int b) {
    search_result result = search_in_cache(myCache, address, s, b, E);

    if (result.hit_line != -1) {
        handle_hit(&myCache->sets[result.set_index], result.hit_line);

        if (vflag) {
            if (op == MODIFY) {
                printf("M %llx,%d hit hit\n", address, size);
            }
        }
    } else {
        handle_miss(&myCache->sets[result.set_index], result.tag, result);

        if (vflag) {
            if (op == MODIFY) {
                printf("M %llx,%d miss", address, size);
                if (eviction_count > 0) {
                    printf(" eviction");
                }
                printf(" hit\n");
            } else {
                printf("%c %llx,%d miss", op, address, size);
                if (eviction_count > 0) {
                    printf(" eviction");
                }
                printf("\n");
            }
        }
    }
}

/***** Memory allocation and Cache Initialization*/

cache initialize_cache(int S, int E) {
    cache newCache;

    // Allocate memory for the sets
    newCache.sets = (cache_set*) malloc(S * sizeof(cache_set));

    // Allocate a contiguous block of memory for all cache lines of all sets
    cache_line* allLines = (cache_line*) malloc(S * E * sizeof(cache_line));

    // Initialize every cache line to default values and assign pointers to appropriate sections
    for (int i = 0; i < S; i++) {
        newCache.sets[i].lines = allLines + (i * E); // Point to the appropriate section in the contiguous block
        for (int j = 0; j < E; j++) {
            newCache.sets[i].lines[j].valid = 0;
            newCache.sets[i].lines[j].tag = 0;
            newCache.sets[i].lines[j].lru = 0;
        }
    }

    return newCache;
}


/***** Free memory allocated for cache *****/

void free_cache(cache *myCache, int S, int E) {
    free(myCache->sets[0].lines);  // free the entire block of cache lines
    free(myCache->sets);  // free the sets
}

/***** Command Line Parsing *****/

int main(int argc, char *argv[]) {
    char *tracefile = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (opt) {
            case 'h':
                hflag = 1;
                break;
            case 'v':
                vflag = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                tracefile = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

     // If help flag is set, print usage info and exit
    if (hflag) {
        fprintf(stderr, "Usage: %s [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n", argv[0]);
        fprintf(stderr, "\nOptions:\n");
        fprintf(stderr, "  -h: Optional help flag that prints usage info\n");
        fprintf(stderr, "  -v: Optional verbose flag that displays trace info\n");
        fprintf(stderr, "  -s <s>: Number of set index bits (S = 2^s is the number of sets)\n");
        fprintf(stderr, "  -E <E>: Associativity (number of lines per set)\n");
        fprintf(stderr, "  -b <b>: Number of block bits (B = 2^b is the block size)\n");
        fprintf(stderr, "  -t <tracefile>: Name of the valgrind trace to replay\n");
    }
        // Check for mandatory arguments
    if (s == 0 || E == 0 || b == 0 || tracefile == NULL) {
        fprintf(stderr, "Error: Missing mandatory argument(s)\n");
        fprintf(stderr, "Usage: %s [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /***** Read File *****/

    myCache = initialize_cache(1 << s, E);
    FILE* trace = fopen(tracefile, "r");
    if (!trace) {
        fprintf(stderr, "Error: Couldn't open %s for reading.\n", tracefile);
        exit(EXIT_FAILURE);
    }

    char operation;
    unsigned long long address;
    int size;

    while (fscanf(trace, " %c %llx,%d", &operation, &address, &size) != EOF) {
        if (operation == 'I') {
            continue;  // Ignore instruction load operations
        }
        process_cache(&myCache, operation, address, size , s, E, b);

        if (operation == MODIFY) {
        process_cache(&myCache, STORE, address, size, s, E, b);
    }
        }
/***** Close out, Print Results, Free Memory *****/

    fclose(trace);
    printSummary(hit_count, miss_count, eviction_count);
    free_cache(&myCache, 1 << s, E);
    return 0;
}