/*
* Copyright (c) 2008, BSC (Barcelon Supercomputing Center)
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of the <organization> nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY BSC ''AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "clapack_used.h"
#include "wf_interface.h"
#include "pp_time.h"

#define BSIZE 64

using obj::object_t;
using obj::inoutdep;

typedef float (*block_t)[BSIZE];
typedef object_t<float[BSIZE][BSIZE]> h_block_t;
typedef inoutdep<float[BSIZE][BSIZE]> binout;

//-------------------------------------------------------------------------------------------------------------------------

#pragma css task input(A, B) inout(C)
void matmul (float A[BSIZE][BSIZE], float B[BSIZE][BSIZE], float C[BSIZE][BSIZE])
{

  int i, j, k;

  for (i = 0; i < BSIZE; i++)
    {
      for (j = 0; j < BSIZE; j++)
	{
	for (k=0; k < BSIZE; k++)
	C[i][j] += A[i][k]*B[k][j];
	}
    }
}

void matmul_wrap (float A[BSIZE][BSIZE], float B[BSIZE][BSIZE], binout C ) {
    matmul( A, B, (block_t)C );
}

//---------------------------------------------------------------------------------------------------------------------------

void compute_ijk(pp_time_t * tm, int DIM, float **A, float **B, h_block_t * C)
{
    int i, j, k;

#pragma css start
    pp_time_start(tm);

    for (i = 0; i < DIM; i++)
	for (j = 0; j < DIM; j++)
	    for (k = 0; k < DIM; k++)
		spawn(matmul_wrap, (float (*)[BSIZE])A[i*DIM+k], (float (*)[BSIZE])B[k*DIM+j], (binout)C[i*DIM+j]);

    ssync();
#pragma css finish
    pp_time_end(tm);

}

void compute_ikj(pp_time_t * tm, int DIM, float **A, float **B, h_block_t * C)
{
    int i, j, k;

#pragma css start
    pp_time_start(tm);

    for (i = 0; i < DIM; i++)
	for (k = 0; k < DIM; k++)
	    for (j = 0; j < DIM; j++)
		spawn(matmul_wrap, (float (*)[BSIZE])A[i*DIM+k], (float (*)[BSIZE])B[k*DIM+j], (binout)C[i*DIM+j]);

    ssync();
#pragma css finish
    pp_time_end(tm);

}

void compute_kij(pp_time_t * tm, int DIM, float **A, float **B, h_block_t * C)
{
    int i, j, k;

#pragma css start
    pp_time_start(tm);

    for (k = 0; k < DIM; k++)
	for (i = 0; i < DIM; i++)
	    for (j = 0; j < DIM; j++)
		spawn(matmul_wrap, (float (*)[BSIZE])A[i*DIM+k], (float (*)[BSIZE])B[k*DIM+j], (binout)C[i*DIM+j]);

    ssync();
#pragma css finish
    pp_time_end(tm);

}

float **A;
float **B;
h_block_t *C;

enum loop_order {
    lo_ijk,
    lo_ikj,
    lo_kij,
    lo_NUM
};

static const char * loop_order_strings[1+(int)lo_NUM] = {
    "ijk",
    "ikj",
    "kij",
    "<invalid>"
};

void initialize (int argc, char **argv, int * N_p, int * DIM_p,
		 loop_order * lo_p);

int main (int argc, char **argv)
{
    // local vars
    int N, DIM;
    loop_order variant;
    unsigned long elapsed;
    pp_time_t tm;
    memset( &tm, 0, sizeof(tm) );


    // application initialization
    initialize( argc, argv, &N, &DIM, &variant );

    // compute with CellSs
    switch( variant ) {
    case lo_ijk:
	run( compute_ijk, &tm, DIM, A, B, C );
	break;
    case lo_ikj:
	run( compute_ikj, &tm, DIM, A, B, C );
	break;
    case lo_kij:
	run( compute_kij, &tm, DIM, A, B, C );
	break;
    case lo_NUM:
	break;
    }

    elapsed = pp_time_read(&tm);
    printf("Matrix dimension: %d\n",N);
    printf("Loop order: %s\n", loop_order_strings[(int)variant] );
    printf ("Time %lu microsecs\n", elapsed);
    printf ("Perf %u MFlops\n", (unsigned)(((double)N*N*N*2)/((double)elapsed)));
    printf("Running time = %g %s\n", pp_time_read(&tm), pp_time_unit() );

    return 0;
}

static void convert_to_blocks(int DIM, int N, float *Alin, float **A)
{
    int i, j;
    for (i = 0; i < N; i++)
	for (j = 0; j < N; j++)
	    A[(i/BSIZE)*DIM+(j/BSIZE)][(i%BSIZE)*BSIZE+(j%BSIZE)] = Alin[j*N+i];
}

static void convert_to_h_blocks(int DIM, int N, float *Alin, h_block_t * A)
{
    int i, j;
    for (i = 0; i < N; i++)
	for (j = 0; j < N; j++) {
	    block_t b = (block_t)A[(i/BSIZE)*DIM+(j/BSIZE)];
	    b[i%BSIZE][j%BSIZE] = Alin[j*N+i];
	}
}

void initialize( int argc, char **argv, int * N_p, int * DIM_p, loop_order * variant_p )
{
    int ISEED[4] = {0,0,0,1};
    int IONE=1;
    int DIM;
    char UPLO='n';
    float FZERO=0.0;
    int i;

    *variant_p = lo_ijk;

    if( argc == 2 )
	DIM = atoi( argv[1] );
    else if( argc == 3 ) {
	DIM = atoi( argv[1] );
	if( !strcmp( argv[2], "ijk" ) )
	    *variant_p = lo_ijk;
	else if( !strcmp( argv[2], "ikj" ) )
	    *variant_p = lo_ikj;
	else if( !strcmp( argv[2], "kij" ) )
	    *variant_p = lo_kij;
	else
	    *variant_p = lo_NUM;
    } else {
	printf( "usage: %s DIM [(ijk|ikj|kij)]\n", argv[0] );
	exit(0);
    }

    // matrix init
    int N=BSIZE*DIM;
    int NN=N*N;

    *N_p=N;
    *DIM_p=DIM;

    // linear matrix
    float *Alin = (float *) malloc(NN * sizeof(float));
    float *Blin = (float *) malloc(NN * sizeof(float));
    float *Clin = (float *) malloc(NN * sizeof(float));

    // fill the matrix with random values
    slarnv_(&IONE, ISEED, &NN, Alin);
    slarnv_(&IONE, ISEED, &NN, Blin);
    slaset_(&UPLO,&N,&N,&FZERO,&FZERO,Clin,&N);

    A = (float **) malloc(DIM*DIM*sizeof(float *));
    B = (float **) malloc(DIM*DIM*sizeof(float *));
    // C = (float **) malloc(DIM*DIM*sizeof(float *));
    C = new h_block_t [DIM*DIM];

    for (i = 0; i < DIM*DIM; i++)
    {
	A[i] = (float *) malloc(BSIZE*BSIZE*sizeof(float));
	B[i] = (float *) malloc(BSIZE*BSIZE*sizeof(float));
	// C[i] = (float *) malloc(BSIZE*BSIZE*sizeof(float));
	//C[i] = new h_block_t;
    }

    convert_to_blocks(DIM, N, Alin, A);
    convert_to_blocks(DIM, N, Blin, B);
    convert_to_h_blocks(DIM, N, Clin, C);

    free(Alin);
    free(Blin);
    free(Clin);
}

