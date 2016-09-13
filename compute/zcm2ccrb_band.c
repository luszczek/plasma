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
    @ingroup plasma_cm2ccrb

    Convert column-major (CM) to tiled (CCRB) layout for a band matrix.
    Out-of-place.
*/
void PLASMA_zcm2ccrb_band_Async(PLASMA_enum uplo,
                                PLASMA_Complex64_t *Af77, int lda, PLASMA_desc *A,
                                PLASMA_sequence *sequence, PLASMA_request *request)
{
    // Get PLASMA context.
    plasma_context_t *plasma = plasma_context_self();
    if (plasma == NULL) {
        plasma_error("PLASMA not initialized");
        plasma_request_fail(sequence, request, PLASMA_ERR_ILLEGAL_VALUE);
        return;
    }

    // Check input arguments.
    if (Af77 == NULL) {
        plasma_error("NULL A");
        plasma_request_fail(sequence, request, PLASMA_ERR_ILLEGAL_VALUE);
        return;
    }
    if (plasma_desc_band_check(uplo, A) != PLASMA_SUCCESS) {
        plasma_error("invalid A");
        plasma_request_fail(sequence, request, PLASMA_ERR_ILLEGAL_VALUE);
        return;
    }
    if (sequence == NULL) {
        plasma_error("NULL sequence");
        plasma_request_fail(sequence, request, PLASMA_ERR_ILLEGAL_VALUE);
        return;
    }
    if (request == NULL) {
        plasma_error("NULL request");
        plasma_request_fail(sequence, request, PLASMA_ERR_ILLEGAL_VALUE);
        return;
    }

    // Check sequence status.
    if (sequence->status != PLASMA_SUCCESS) {
        plasma_request_fail(sequence, request, PLASMA_ERR_SEQUENCE_FLUSHED);
        return;
    }

    // quick return with success
    if (A->m == 0 || A->n == 0)
        return;

    // Call the parallel function.
    plasma_pzoocm2ccrb_band(uplo, Af77, lda, *A, sequence, request);

    return;
}