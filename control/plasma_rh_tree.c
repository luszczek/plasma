/**
 *
 * @file
 *
 *  PLASMA is a software package provided by:
 *  University of Tennessee, US,
 *  University of Manchester, UK.
 **/

#include "plasma_descriptor.h"
#include "plasma_internal.h"
#include "plasma_rh_tree.h"

void plasma_rh_tree_greedy(int mt, int nt, int **operations, int *noperations);
void plasma_rh_tree_plasmatree(int mt, int nt,
                               int **operations, int *noperations);
void plasma_rh_tree_flat(int mt, int nt,
                         int **operations, int *noperations);


/***************************************************************************//**
 *  Routine for precomputing a given order of operations for tile 
 *  QR and LQ factorization.
 * @see plasma_omp_zgeqrf
 **/
void plasma_rh_tree_operations(int mt, int nt,
                               int **operations, int *noperations)
{
    // Different algorithms can be implemented and switched here:
    
    // Flat tree as in the standard geqrf routine.
    // Combines only GE and TS kernels.
    plasma_rh_tree_flat(mt, nt, operations, noperations);
    
    // PLASMA-Tree from PLASMA 2.8.0
    //plasma_rh_tree_plasmatree(mt, nt, operations, noperations);

    // Pure Greedy algorithm combining only GE and TT kernels.
    //plasma_rh_tree_greedy(mt, nt, operations, noperations);
}


/***************************************************************************//**
 *  Parallel tile QR factorization based on the GREEDY algorithm from 
 *  H. Bouwmeester, M. Jacquelin, J. Langou, Y. Robert
 *  Tiled QR factorization algorithms. INRIA Report no. 7601, 2011.
 * @see plasma_omp_zgeqrf
 **/
void plasma_rh_tree_greedy(int mt, int nt, int **operations, int *noperations)
{
    static const int debug = 0;

    // How many columns to involve?
    int minnt = imin(mt, nt);

    // Tiles above diagonal are not triangularized.
    int num_triangularized_tiles  = mt*minnt - (minnt-1)*minnt/2; 
    // Tiles on diagonal and above are not anihilated.
    int num_anihilated_tiles      = mt*minnt - (minnt+1)*minnt/2; 

    // Number of operations can be determined exactly.
    int nops = num_triangularized_tiles + num_anihilated_tiles;

    // Allocate array of operations.
    *operations = (int *) malloc(nops*4*sizeof(int));

    // Prepare memory for column counters.
    int *NZ = (int*) malloc(minnt*sizeof(int));
    int *NT = (int*) malloc(minnt*sizeof(int));

    // Initialize column counters.
    for (int j = 0; j < minnt; j++) {
        // NZ[j] is the number of tiles which have been eliminated in column j
        NZ[j] = 0;
        // NT[j] is the number of tiles which have been triangularized in column j
        NT[j] = 0;
    }

    int nZnew = 0; 
    int nTnew = 0;
    int iops  = 0;
    // Until the last column is finished...
    while ((NT[minnt-1] < mt - minnt + 1) ||
           (NZ[minnt-1] < mt - minnt)    ) {
        for (int j = minnt-1; j >= 0; j--) {

            if (j == 0) {
                // Triangularize the first column if not yet done.
                nTnew = NT[j] + (mt-NT[j]);
                if (mt - NT[j] > 0) {
                    for (int k = mt - 1; k >= 0; k--) {

                        // GEQRT(k,j)
                        if (debug) printf("GEQRT (%d,%d) ", k, j);
                        plasma_rh_tree_operation_insert(*operations, iops,
                                                        PlasmaGEKernel,
                                                        j, k, -1);
                        iops++;
                        if (debug) printf("\n ");
                    }
                }
            }
            else {
                // Triangularize every tile having zero in the previous column.
                nTnew = NZ[j-1];
                for (int k = NT[j]; k < nTnew; k++) {
                    int kk = mt-k-1;

                    // GEQRT(kk,j)
                    if (debug) printf("GEQRT (%d,%d) ", kk, j);
                    plasma_rh_tree_operation_insert(*operations, iops,
                                                    PlasmaGEKernel,
                                                    j, kk, -1);
                    iops++;

                    if (debug) printf("\n ");
                }
            }

            // Eliminate every tile triangularized in the previous step.
            int batch = (NT[j] - NZ[j]) / 2; // intentional integer division
            nZnew = NZ[j] + batch;
            for (int kk = NZ[j]; kk < nZnew; kk++) {

                int pmkk    = mt-kk-1;  // row index of a tile to be zeroed
                int pivpmkk = pmkk-batch; // row index of the anihilator tile

                // TTQRT(mt- kk - 1, pivpmkk, j)
                if (debug) printf("TTQRT (%d,%d,%d) ", pmkk, pivpmkk, j);
                plasma_rh_tree_operation_insert(*operations, iops,
                                                PlasmaTTKernel,
                                                j, pmkk, pivpmkk);
                iops++;

                if (debug) printf("\n ");
            }
            // Update the number of triangularized and eliminated tiles at the
            // next step.
            NT[j] = nTnew;
            NZ[j] = nZnew;
        }
    }

    // Check that we have reached the expected number of operations.
    if (iops != nops) {
        printf("I have not reached the expected number of operations.");
    }

    // Copy over the number of operations.
    *noperations = iops;

    // Deallocate column counters.
    free(NZ);
    free(NT);
}

/***************************************************************************//**
 *  Parallel tile communication avoiding QR factorization from 
 *  PLASMA version 2.8.0.
 *  Also known as PLASMA-TREE, it combines TS kernels within 
 *  blocks of tiles of height BS and TT kernels on top of these blocks in 
 *  a binary-tree fashion.
 * @see plasma_omp_zgeqrf
 **/
void plasma_rh_tree_plasmatree(int mt, int nt,
                               int **operations, int *noperations)
{
    static const int debug = 0;

    static const int BS = 4;

    // How many columns to involve?
    int minnt = imin(mt, nt);

    // Tiles above diagonal are not triangularized.
    int num_triangularized_tiles  = ((mt/BS)+1)*minnt;
    // Tiles on diagonal and above are not anihilated.
    int num_anihilated_tiles      = mt*minnt - (minnt+1)*minnt/2; 

    // An upper bound on the number of operations.
    int nops = num_triangularized_tiles + num_anihilated_tiles;

    // Allocate array of operations.
    *operations = (int *) malloc(nops*4*sizeof(int));

    // Initialize the array of operations.
    //for (int i = 1; i < nops; i++) {
    //    plasma_rh_tree_operation_insert(*operations, i,
    //                                    -1, -1, -1, -1);
    //}

    // Counter of number of inserted operations.
    int iops = 0;
    for (int k = 0; k < minnt; k++) {
        for (int M = k; M < mt; M += BS) {
            //geqrt(A(M,k), T(M,k))
            if (debug) printf("GEQRT (%d,%d) ", M, k);
            plasma_rh_tree_operation_insert(*operations, iops,
                                            PlasmaGEKernel,
                                            k, M, -1);
            iops++;
            for (int m = M+1; m < imin(M+BS, mt); m++) {
                //ztsqrt(A(M, k), A(m, k), T(m, k))
                if (debug) printf("TSQRT (%d,%d,%d) ", m, M, k);
                plasma_rh_tree_operation_insert(*operations, iops,
                                                PlasmaTSKernel,
                                                k, m, M);
                iops++;
            }
        }
        for (int rd = BS; rd < mt-k; rd *= 2) {
            for (int M = k; M+rd < mt; M += 2*rd) {
                //zttqrt(A(M,k), A(M+rd,k), T2(M+rd,k))
                if (debug) printf("TTQRT (%d,%d,%d)", M+rd, M, k);
                plasma_rh_tree_operation_insert(*operations, iops,
                                                PlasmaTTKernel,
                                                k, M+rd, M);
                iops++;
            }
        }
    }

    if (iops > nops) {
        printf("I have exceeded the expected number of operations.");
    }

    // Copy over the number of operations.
    *noperations = iops;
}

/***************************************************************************//**
 *  Parallel tile QR factorization using the flat tree.
 *  This is the simplest tile-QR algorithm based on 
 *  TS (Triangle on top of Square) kernels.
 *  Implemented directly in the pzgeqrf and pzgelqf routines, it is included 
 *  here mostly for debugging purposes.
 * @see plasma_omp_zgeqrf
 **/
void plasma_rh_tree_flat(int mt, int nt,
                         int **operations, int *noperations)
{
    static const int debug = 0;

    // How many columns to involve?
    int minnt = imin(mt, nt);

    // Only diagonal tiles are triangularized.
    int num_triangularized_tiles  = minnt;
    // Tiles on diagonal and above are not anihilated.
    int num_anihilated_tiles      = mt*minnt - (minnt+1)*minnt/2; 

    // Number of operations can be directly computed.
    int nops = num_triangularized_tiles + num_anihilated_tiles;

    // Allocate array of operations.
    *operations = (int *) malloc(nops*4*sizeof(int));

    // Initialize the array of operations.
    for (int i = 1; i < nops; i++) {
        plasma_rh_tree_operation_insert(*operations, i,
                                        -1, -1, -1, -1);
    }

    // Counter of number of inserted operations.
    int iops = 0;
    for (int k = 0; k < minnt; k++) {
        //zgeqrt(A(k, k))
        if (debug) printf("GEQRT (%d,%d) ", k, k);
        plasma_rh_tree_operation_insert(*operations, iops,
                                        PlasmaGEKernel,
                                        k, k, -1);
        iops++;

        for (int m = k+1; m < mt; m++) {
            //ztsqrt(A(m, k), A(k, k))
            if (debug) printf("TSQRT (%d,%d,%d) ", m, k, k);
            plasma_rh_tree_operation_insert(*operations, iops,
                                            PlasmaTSKernel,
                                            k, m, k);
            iops++;
        }
    }

    if (iops != nops) {
        printf("I have exceeded the expected number of operations.");
    }

    // Copy over the number of operations.
    *noperations = iops;
}
