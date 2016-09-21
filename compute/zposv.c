/**
 *
 * @file
 *
 *  PLASMA is a software package provided by:
 *  University of Tennessee, US,
 *  University of Manchester, UK.
 *
 * @precisions normal z -> s d c
 *
 **/

#include "plasma_types.h"
#include "plasma_async.h"
#include "plasma_context.h"
#include "plasma_descriptor.h"
#include "plasma_internal.h"
#include "plasma_z.h"

/***************************************************************************//**
 *
 * @ingroup plasma_posv
 *
 *  Computes the solution to a system of linear equations A * X = B,
 *  where A is an n-by-n Hermitian positive definite matrix and X and B are
 *  n-by-nrhs matrices. The Cholesky decomposition is used to factor A as
 *
 *    \f[ A =  L\times L^H, \f] if uplo = PlasmaLower,
 *    or
 *    \f[ A =  U^H\times U, \f] if uplo = PlasmaUpper,
 *
 *  where U is an upper triangular matrix and  L is a lower triangular matrix.
 *  The factored form of A is then used to solve the system of equations:
 *
 *   A * X = B.
 *
 *******************************************************************************
 *
 * @param[in] uplo
 *          - PlasmaUpper: Upper triangle of A is stored;
 *          - PlasmaLower: Lower triangle of A is stored.
 *
 * @param[in] n
 *          The number of linear equations, i.e., the order of the matrix A.
 *          n >= 0.
 *
 * @param[in] nrhs
 *          The number of right hand sides, i.e., the number of columns
 *          of the matrix B.  nrhs >= 0.
 *
 * @param[in,out] A
 *          On entry, the Hermitian positive definite matrix A.
 *          If uplo = PlasmaUpper, the leading n-by-n upper triangular part of A
 *          contains the upper triangular part of the matrix A, and the strictly
 *          lower triangular part of A is not referenced.
 *          If UPLO = 'L', the leading n-by-n lower triangular part of A
 *          contains the lower triangular part of the matrix A, and the strictly
 *          upper triangular part of A is not referenced.
 *          On exit, if return value = 0, the factor U or L from
 *          the Cholesky factorization  A = U^H*U or A = L*L^H.
 *
 * @param[in] lda
 *          The leading dimension of the array A. lda >= max(1,n).
 *
 * @param[in,out] B
 *          On entry, the n-by-nrhs right hand side matrix B.
 *          On exit, if return value = 0, the n-by-nrhs solution matrix X.
 *
 * @param[in] ldb
 *          The leading dimension of the array B. ldb >= max(1,n).
 *
 *******************************************************************************
 *
 * @retval PlasmaSuccess successful exit
 * @retval  < 0 if -i, the i-th argument had an illegal value
 * @retval  > 0 if i, the leading minor of order i of A is not
 *          positive definite, so the factorization could not
 *          be completed, and the solution has not been computed.
 *
 *******************************************************************************
 *
 * @sa plasma_omp_zposv
 * @sa PLASMA_cposv
 * @sa PLASMA_dposv
 * @sa PLASMA_sposv
 *
 ******************************************************************************/
int PLASMA_zposv(plasma_enum_t uplo, int n, int nrhs,
                 plasma_complex64_t *A, int lda,
                 plasma_complex64_t *B, int ldb)
{
    int nb;
    int status;
    int retval;

    plasma_desc_t descA;
    plasma_desc_t descB;

    // Get PLASMA context.
    plasma_context_t *plasma = plasma_context_self();
    if (plasma == NULL) {
        plasma_error("PLASMA not initialized");
        return PlasmaErrorNotInitialized;
    }

    // Check input arguments
    if ((uplo != PlasmaUpper) &&
        (uplo != PlasmaLower)) {
        plasma_error("illegal value of uplo");
        return -1;
    }
    if (n < 0) {
        plasma_error("illegal value of n");
        return -2;
    }
    if (nrhs < 0) {
        plasma_error("illegal value of nrhs");
        return -3;
    }
    if (lda < imax(1, n)) {
        plasma_error("illegal value of lda");
        return -5;
    }
    if (ldb < imax(1, n)) {
        plasma_error("illegal value of ldb");
        return -7;
    }
    // Quick return - currently NOT equivalent to LAPACK's
    //LAPACK does not have such check for DPOSV
    //
    //if (min(n, nrhs) == 0)
    //    return PlasmaSuccess;

    // Tune.
    //status = plasma_tune(PLASMA_FUNC_ZPOSV, N, N, nrhs);
    //if (status != PlasmaSuccess) {
    //   plasma_error("PLASMA_zposv", "plasma_tune() failed");
    //    return status;
    // }
    nb    = plasma->nb;

    // Create tile matrices.
    retval = plasma_desc_general_create(PlasmaComplexDouble, nb, nb,
                                        lda, n, 0, 0, n, n, &descA);
    if (retval != PlasmaSuccess) {
        plasma_error("plasma_desc_general_create() failed");
        return retval;
    }
    retval = plasma_desc_general_create(PlasmaComplexDouble, nb, nb,
                                        ldb, nrhs, 0, 0, n, nrhs, &descB);
    if (retval != PlasmaSuccess) {
        plasma_error("plasma_desc_general_create() failed");
        plasma_desc_destroy(&descA);
        return retval;
    }

    // Create sequence.
    plasma_sequence_t *sequence = NULL;
    retval = plasma_sequence_create(&sequence);
    if (retval != PlasmaSuccess) {
        plasma_error("plasma_sequence_create() failed");
        return retval;
    }
    // Initialize request.
    plasma_request_t request = PLASMA_REQUEST_INITIALIZER;

    #pragma omp parallel
    #pragma omp master
    {
        // Translate to tile layout.
        PLASMA_zcm2ccrb_Async(A, lda, &descA, sequence, &request);
        PLASMA_zcm2ccrb_Async(B, ldb, &descB, sequence, &request);

        // Call the tile async function.
        plasma_omp_zposv(uplo,
                         &descA,
                         &descB,
                         sequence, &request);

        // Translate back to LAPACK layout.
        PLASMA_zccrb2cm_Async(&descB, B, ldb, sequence, &request);
    }
    // implicit synchronization

    // Free matrices in tile layout.
    plasma_desc_destroy(&descA);
    plasma_desc_destroy(&descB);

    // Return status.
    status = sequence->status;
    plasma_sequence_destroy(sequence);
    return status;
}

/***************************************************************************//**
 *
 * @ingroup plasma_posv
 *
 *  Solves a Hermitian positive definite system of linear equations
 *  using Cholesky factorization.
 *  Non-blocking tile version of PLASMA_zposv().
 *  Operates on matrices stored by tiles.
 *  All matrices are passed through descriptors.
 *  All dimensions are taken from the descriptors.
 *  Allows for pipelining of operations at runtime.
 *
 *******************************************************************************
 *
 * @param[in] uplo
 *          - PlasmaUpper: Upper triangle of A is stored;
 *          - PlasmaLower: Lower triangle of A is stored.
 *
 * @param[in,out] A
 *          On entry, the Hermitian positive definite matrix A.
 *          If uplo = PlasmaUpper, the leading n-by-n upper triangular part of A
 *          contains the upper triangular part of the matrix A, and the strictly
 *          lower triangular part of A is not referenced.
 *          If UPLO = 'L', the leading n-by-n lower triangular part of A
 *          contains the lower triangular part of the matrix A, and the strictly
 *          upper triangular part of A is not referenced.
 *          On exit, if return value = 0, the factor U or L from
 *          the Cholesky factorization  A = U^H*U or A = L*L^H.
 *
 * @param[in,out] B
 *          On entry, the n-by-nrhs right hand side matrix B.
 *          On exit, if return value = 0, the n-by-nrhs solution matrix X.
 *
 * @param[in] sequence
 *          Identifies the sequence of function calls that this call belongs to
 *          (for completion checks and exception handling purposes).  Check
 *          the sequence->status for errors.

 * @param[out] request
 *          Identifies this function call (for exception handling purposes).
 *
 * @retval void
 *          Errors are returned by setting sequence->status and
 *          request->status to error values.  The sequence->status and
 *          request->status should never be set to PlasmaSuccess (the
 *          initial values) since another async call may be setting a
 *          failure value at the same time.
 *
 *******************************************************************************
 *
 * @sa PLASMA_zposv
 * @sa plasma_omp_cposv
 * @sa plasma_omp_dposv
 * @sa plasma_omp_sposv
 *
 ******************************************************************************/
void plasma_omp_zposv(plasma_enum_t uplo,
                      plasma_desc_t *A,
                      plasma_desc_t *B,
                      plasma_sequence_t *sequence, plasma_request_t *request)
{
    // Get PLASMA context.
    plasma_context_t *plasma = plasma_context_self();
    if (plasma == NULL) {
        plasma_error("PLASMA not initialized");
        plasma_request_fail(sequence, request, PlasmaErrorIllegalValue);
        return;
    }

    // Check input arguments.
    if ((uplo != PlasmaUpper) &&
        (uplo != PlasmaLower)) {
        plasma_error("illegal value of uplo");
        return;
    }
    if (plasma_desc_check(A) != PlasmaSuccess) {
        plasma_request_fail(sequence, request, PlasmaErrorIllegalValue);
        plasma_error("invalid A");
        return;
    }
    if (plasma_desc_check(B) != PlasmaSuccess) {
        plasma_error("invalid B");
        plasma_request_fail(sequence, request, PlasmaErrorIllegalValue);
        return;
    }
    if (sequence == NULL) {
        plasma_error("NULL sequence");
        plasma_request_fail(sequence, request, PlasmaErrorIllegalValue);
        return;
    }
    if (request == NULL) {
        plasma_error("NULL request");
        plasma_request_fail(sequence, request, PlasmaErrorIllegalValue);
        return;
    }
/*
    // quick return
    if (min(n, nrhs == 0)
        return PlasmaSuccess;
*/
    // Call the parallel functions.
    plasma_pzpotrf(uplo, *A, sequence, request);

    plasma_complex64_t zone = 1.0;
    plasma_enum_t trans = uplo == PlasmaUpper ? PlasmaConjTrans : PlasmaNoTrans;

    plasma_pztrsm(PlasmaLeft, uplo,
                  trans, PlasmaNonUnit,
                  zone,
                  *A,
                  *B,
                  sequence, request);

    trans = uplo == PlasmaUpper ? PlasmaNoTrans : PlasmaConjTrans;
    plasma_pztrsm(PlasmaLeft, uplo,
                  trans, PlasmaNonUnit,
                  zone,
                  *A,
                  *B,
                  sequence, request);
}
