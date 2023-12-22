/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 


/****** Dillon Gaughan *****/
/* I borrowed my layout from * Chad Underhill and Sam Coache cjunderhill-sccoache. I
 liked their structure but found that only their transpose_other function performed
 better than what I already had. Originally, the switch case was only checking one axis
 and I felt that it might lead to unforseen bugs if thoroughly tested. In my final step
 I was looking for a more optimized version of my transpose_64. I found a program written
 by Coordinate36 and it worked perfectly. I hadn't thought to seperate transposition into
 quadrants and found that it drastically improved miss rate. I did change the naming
 conventions of the functions to follow my own protocol. *****/

#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);
void transpose_32(int M, int N, int A[N][M], int B[M][N]);
void transpose_64(int M, int N, int A[N][M], int B[M][N]);
void transpose_other(int M, int N, int A[N][M], int B[M][N]);

/***** Conditional function calling to create a more organized code layout. *****/

char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
    if (N == 32 && M == 32) {
        // 32x32 matrix
        transpose_32(M, N, A, B);
    } else if (N == 64 && M == 64) {
        // 64x64 matrix
        transpose_64(M, N, A, B);
    } else {
        // All other matrices
        transpose_other(M, N, A, B);
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/***** Loops over 8x8 sized blocks of the matrix, integrates
diagonal handling for further optimization. *****/

char transpose_32_desc[] = "Transpose a 32x32 matrix";
void transpose_32(int M, int N, int A[N][M], int B[M][N]){
    int i, j, ii, jj;
    int diag, tmp;
    for (i = 0; i < N; i += 8) {
        for (j = 0; j < M; j += 8) {
            for (ii = i; ii < i + 8; ++ii) {
                for (jj = j; jj < j + 8; ++jj) {
                    if (ii != jj) {
                        B[jj][ii] = A[ii][jj];
                    } else {
                        diag = ii;
                        tmp = A[ii][jj];
                    }
                }
                if (i == j) {
                    B[diag][diag] = tmp;
                }
            }
        }
    }
}
     

/***** Blocks are broken down from 8x8 further into 4x4 halves. Two different
loops handle transposition. ii handles the 'left above' quadrant. jj
handles everything else as listed in the comments. There is implemented
diagonal handling to further decrease miss count. *****/

char transpose_64_desc[] = "Transpose a 64x64 matrix";
void transpose_64(int M, int N, int A[N][M], int B[M][N]){
    int i, j, ii, jj; 
    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    for (j = 0; j < M; j += 8) {
        for (i = 0; i < N; i += 8) {
            for (ii = i; ii < i + 4; ii++) {
                tmp0 = A[ii][j];
                tmp1 = A[ii][j + 1];
                tmp2 = A[ii][j + 2];
                tmp3 = A[ii][j + 3];
                tmp4 = A[ii][j + 4];
                tmp5 = A[ii][j + 5];
                tmp6 = A[ii][j + 6];
                tmp7 = A[ii][j + 7];
                //left-above, store right above
                B[j][ii] = tmp0;
                B[j + 1][ii] = tmp1;
                B[j + 2][ii] = tmp2;
                B[j + 3][ii] = tmp3;
                B[j][ii + 4] = tmp4;
                B[j + 1][ii + 4] = tmp5;
                B[j + 2][ii + 4] = tmp6;
                B[j + 3][ii + 4] = tmp7;
                }
            for (jj = j; jj < j + 4; jj++) {

                    // A left-down
                tmp4 = A[i + 4][jj];
                tmp5 = A[i + 5][jj];
                tmp6 = A[i + 6][jj];
                tmp7 = A[i + 7][jj];

                    // B right-above
                tmp0 = B[jj][i + 4];
                tmp1 = B[jj][i + 5];
                tmp2 = B[jj][i + 6];
                tmp3 = B[jj][i + 7];

                    // set B right-above
                B[jj][i + 4] = tmp4;
                B[jj][i + 5] = tmp5;
                B[jj][i + 6] = tmp6;
                B[jj][i + 7] = tmp7;

                    // set B left-down
                B[jj + 4][i] = tmp0;
                B[jj + 4][i + 1] = tmp1;
                B[jj + 4][i + 2] = tmp2;
                B[jj + 4][i + 3] = tmp3;

                    // set B right-down
                B[jj + 4][i + 4] = A[i + 4][jj + 4];
                B[jj + 4][i + 5] = A[i + 5][jj + 4];
                B[jj + 4][i + 6] = A[i + 6][jj + 4];
                B[jj + 4][i + 7] = A[i + 7][jj + 4];
            }        
        }   
    }
}

/***** Edge case handing(not 32 or 64). Takes into account matrices
not divisible by 16. Diagonal handling is introduced to optimize cache. *****/

char transpose_other_desc[] = "Transpose any matrix that isn't 32x32 or 64x64";
void transpose_other(int M, int N, int A[N][M], int B[M][N]){
	int i, j, ii, jj;
	int d_val = 0;	
	int diag = 0;	 
	for (jj = 0; jj < M; jj += 16) {
		for (ii = 0; ii < N; ii += 16) {
			for (i = ii; (i < ii + 16) && (i < N); i++) {
				for (j = jj; (j < jj + 16) && (j < M); j++) {
					if (i != j) {
						B[j][i] = A[i][j];
					} else {
						// Assign diagonal element to a temporary variable
						// This saves an individual cache miss on each run through the matrix where the columns and rows still match up
						diag = i;
						d_val = A[i][j];
					}
				}
				// If row and column are same, element is defined as a diagonal and our temporarily saved element is assigned
				if (ii == jj) {
					B[diag][diag] = d_val;
				}
			}
	 	}
	}
}

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}
