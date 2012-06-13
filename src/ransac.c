#include <assert.h>
#include <stdbool.h>
#include <math.h>

#include "fail.c"
#include "xmalloc.c"

// generic function
// evaluate the error of a datapoint according to a model
typedef float (ransac_error_evaluation_function)(
		float *model,
		float *datapoint,
		void *usr
		);


// generic function
// compute the model defined from a few data points
typedef void (ransac_model_generating_function)(
		float *out_model,  // parameters of the computed model
		float *data,       // data points
		void *usr
		);

// generic function
// tell whether a given model is good enough (e.g., not severely distorted)
typedef bool (ransac_model_accepting_function)(
		float *model,
		void  *usr);


// API function: evaluate a given model over the data, and fill a mask with the
// inliers (according to the given allowed error).  This function returns the
// number of inliers.
int ransac_trial(
		// output
		bool *out_mask,    // array mask identifying the inliers

		// input data
		float *data,       // array of input data
		float *model,      // parameters of the model
		float max_error,   // maximum allowed error

		// input context
		int datadim,       // dimension of each data point
		int n,             // number of data points
		ransac_error_evaluation_function *mev,

		// decoration
		void *usr
		)
{
	int cx = 0;
	for (int i = 0; i < n; i++)
	{
		float *datai = data + i*datadim;
		float e = mev(model, datai, usr);
		assert(e >= 0);
		out_mask[i] = e < max_error;
		if (out_mask[i])
			cx += 1;
	}
	return cx;
}

// utility function: return a random number in the interval [a, b)
static int random_index(int a, int b)
{
	int r = a + rand()%(b - a);
	assert(r >= a);
	assert(r < b);
	return r;
}

// comparison function for the qsort call below
static int compare_ints(const void *aa, const void *bb)
{
	const int *a = (const int *)aa;
	const int *b = (const int *)bb;
	return (*a > *b) - (*a < *b);
}

// check whether a vector of n ints has different entries
// (sorts the vector inplace)
static bool are_different(int *t, int n)
{
	qsort(t, n, sizeof*t, compare_ints);
	for (int i = 1; i < n; i++)
		if (t[i-1] == t[i])
			return false;
	return true;
}

// generate a set of n different ints between a and b
static void fill_random_indices(int *idx, int n, int a, int b)
{
	// TODO fisher yates shuffle and traverse it by blocks of length nfit
	int safecount = 0;
	do {
		for (int i = 0; i < n; i++)
			idx[i] = random_index(a, b);
		safecount += 1;
	} while (safecount < 10 && !are_different(idx, n));
	if (safecount == 10)
		fail("could not generate any model");
}


// RANSAC
//
// Given a list of data points, find the parameters of a model that fits to
// those points.  Several models are tried, and the model with the highest
// number of inliers is kept.
//
// A basic idea of this kind of ransac is that a maximum allowed error is fixed
// by hand, and then the inliers of a model are defined as the data points
// which fit the model up to the allowed error.  The RANSAC algorithm randomly
// tries several models and keeps the one with the largest number of inliers.
int ransac(
		// output
		//int *out_ninliers, // number of inliers
		bool *out_mask,    // array mask identifying the inliers
		float *out_model,  // model parameters

		// input data
		float *data,       // array of input data

		// input context
		int datadim,       // dimension of each data point
		int n,             // number of data points
		int modeldim,      // number of model parameters
		ransac_error_evaluation_function *mev,
		ransac_model_generating_function *mgen,
		int nfit,          // data points needed to produce a model

		// input parameters
		int ntrials,       // number of models to try
		int min_inliers,   // minimum allowed number of inliers
		float max_error,   // maximum allowed error

		// decoration
		ransac_model_accepting_function *macc,
		void *usr
		)
{
	int best_ninliers = 0;
	float best_model[modeldim];
	bool *best_mask = xmalloc(n * sizeof*best_mask);
	bool *tmp_mask = xmalloc(n * sizeof*best_mask);

	for (int i = 0; i < ntrials; i++)
	{
		int indices[nfit];
		fill_random_indices(indices, nfit, 0, n);

		float x[nfit*datadim];
		for (int j = 0; j < nfit; j++)
		for (int k = 0; k < datadim; k++)
			x[datadim*j + k] = data[datadim*indices[j] + k];

		float model[modeldim];
		mgen(model, x, usr);
		if (macc && !macc(model, usr))
			continue;

		int n_inliers = ransac_trial(tmp_mask, data, model, max_error,
				datadim, n, mev, usr);

		if (n_inliers > best_ninliers)
		{
			best_ninliers = n_inliers;
			for(int j = 0; j < modeldim; j++)
				best_model[j] = model[j];
			for(int j = 0; j < n; j++)
				best_mask[j] = tmp_mask[j];
		}
	}

	if (best_ninliers >= min_inliers)
	{
		if (out_model)
			for(int j = 0; j < modeldim; j++)
				out_model[j] = best_model[j];
		if (out_mask)
			for(int j = 0; j < n; j++)
				out_mask[j] = best_mask[j];
		return best_ninliers;
	} else
		return 0;
}




#ifndef OMIT_MAIN

#include <stdio.h>
#include <string.h>

#include "ransac_cases.c" // example functions for RANSAC input
#include "parsenumbers.c" // function "read_ascii_floats"

int main_cases(int c, char *v[])
{
	if (c < 5) {
		fprintf(stderr, "usage:\n\t%s {line,aff,affn,fm} "
		//                         0   1
			"ntrials maxerr minliers [inliers] <data\n", *v);
		//       2       3      4       5
		return EXIT_FAILURE;
	}

	// parse input options
	char *model_id = v[1];
	int ntrials = atoi(v[2]);
	float maxerr = atof(v[3]);
	int minliers = atoi(v[4]);
   FILE *inliers = c>=5? fopen(v[5],"w"):NULL;

	// declare context variables
	int modeldim, datadim, nfit;
	ransac_error_evaluation_function *model_evaluation;
	ransac_model_generating_function *model_generation;
	ransac_model_accepting_function *model_acceptation = NULL;
	void *user_data = NULL;


	// fill context variables according to ransac case
	if (0 == strcmp(model_id, "line")) {
		datadim = 2;
		modeldim = 3;
		nfit = 2;
		model_evaluation = distance_of_point_to_straight_line;
		model_generation = straight_line_through_two_points;

	} else if (0 == strcmp(model_id, "aff")) {
		datadim = 4;
		modeldim = 6;
		nfit = 3;
		model_evaluation = affine_match_error;
		model_generation = affine_map_from_three_pairs;

	} else if (0 == strcmp(model_id, "affn")) {
		datadim = 4;
		modeldim = 6;
		nfit = 3;
		model_evaluation = affine_match_error;
		model_generation = affine_map_from_three_pairs;
		model_acceptation = affine_map_is_reasonable;

	} else if (0 == strcmp(model_id, "hom")) {
		datadim = 4;
		modeldim = 9;
		nfit = 4;
		fail("not yet implemented");
		//model_evaluation = homographic_match_error;
		//model_generation = homography_from_four;
		//model_acceptation = homography_is_reasonable;

	} else if (0 == strcmp(model_id, "fm")) { // fundamental matrix
		datadim = 4;
		modeldim = 9;
		nfit = 7;
		model_evaluation = epipolar_algebraic_error;
		model_generation = seven_point_algorithm;
		//model_acceptation = fundamental_matrix_is_reasonable;

	} else {
		printf("unrecognized model \"%s\"\n", model_id);
		return EXIT_FAILURE;
	}

	// read input data
	int n;
	float *data = read_ascii_floats(stdin, &n);
	n /= datadim;

	// call the ransac function to fit a model to data
	float model[modeldim];
	bool *mask = xmalloc(n * sizeof*mask);
	int n_inliers = ransac(mask, model, data, datadim, n, modeldim,
			model_evaluation, model_generation,
			nfit, ntrials, minliers, maxerr,
			model_acceptation, user_data);


	// print a summary of the results
	if (n_inliers > 0) {
		printf("RANSAC found a model with %d inliers\n", n_inliers);
		printf("parameters =");
		for (int i = 0; i < modeldim; i++)
			printf(" %g", model[i]);
      printf("\n");
      // if the file is provided print the inliers
      if (inliers) {
         for(int i = 0; i < n; i++){
            if( mask[i] ) {
               for(int d = 0; d < datadim; d++)
                  fprintf(inliers,"%g ",data[i*datadim+d]);
               fprintf(inliers,"\n");
            }
         }
      }
	} else printf("RANSAC found no model\n");

   if (inliers) fclose(inliers);

	return EXIT_SUCCESS;
}

int main(int c, char *v[])
{
	return main_cases(c, v);
}
#endif//OMIT_MAIN