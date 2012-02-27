#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <time.h>

/**
 *  Includes required for BOINC
 */
#ifdef _BOINC_
#ifdef _WIN32
    #include "boinc_win.h"
    #include "str_util.h"
#endif

    #include "diagnostics.h"
    #include "util.h"
    #include "filesys.h"
    #include "boinc_api.h"
    #include "mfile.h"
#endif


const unsigned int ELEMENT_SIZE = sizeof(unsigned int) * 8;

unsigned long int max_sums_length;
unsigned int *sums;
unsigned int *new_sums;

FILE *output_target;

/**
 *  Print the bits in an unsigned int.  Note this prints out from right to left (not left to right)
 */
void print_bits(const unsigned int number) {
    unsigned int pos = 1 << (ELEMENT_SIZE) - 1;
    while (pos > 0) {
        if (number & pos) fprintf(output_target, "1");
        else fprintf(output_target, "0");
        pos >>= 1;
    }
}

/**
 * Print out an array of bits
 */
void print_bit_array(const unsigned int *bit_array, const unsigned int bit_array_length) {
    for (unsigned int i = 0; i < bit_array_length; i++) {
        print_bits(bit_array[i]);
    }
}

/**
 *  Print out all the elements in a subset
 */
void print_subset(const unsigned int *subset, const unsigned int subset_size) {
    fprintf(output_target, "[");
    for (unsigned int i = 0; i < subset_size; i++) {
        fprintf(output_target, "%4u", subset[i]);
    }
    fprintf(output_target, "]");
}

/**
 * Print out an array of bits, coloring the required subsets green, if there is a missing sum (a 0) it is colored red
 */
void print_bit_array_color(const unsigned int *bit_array, unsigned long int max_sums_length, unsigned int min, unsigned int max) {
    unsigned int msl = max_sums_length * ELEMENT_SIZE;
    unsigned int number, pos;
    unsigned int count = 0;

//    fprintf(output_target, " - MSL: %u, MIN: %u, MAX: %u - ", msl, min, max);

    bool red_on = false;

//    fprintf(output_target, " msl - min [%u], msl - max [%u] ", (msl - min), (msl - max));

    for (unsigned int i = 0; i < max_sums_length; i++) {
        number = bit_array[i];
        pos = 1 << (ELEMENT_SIZE) - 1;

        while (pos > 0) {
            if ((msl - min) == count) {
                red_on = true;
                fprintf(output_target, "\e[32m");
            }

            if (number & pos) fprintf(output_target, "1");
            else {
                if (red_on) {
                    fprintf(output_target, "\e[31m0\e[32m");
                } else {
                    fprintf(output_target, "0");
                }
            }

            if ((msl - max) == count) {
                fprintf(output_target, "\e[0m");
                red_on = false;
            }

            pos >>= 1;
            count++;
        }
    }
}

/**
 *  Shift all the bits in an array of to the left by shift. src is unchanged, and the result of the shift is put into dest.
 *  length is the number of elements in dest (and src) which should be the same for both
 *
 *  Performs:
 *      dest = src << shift
 */
static inline void shift_left(unsigned int *dest, const unsigned int length, const unsigned int *src, const unsigned int shift) {
    unsigned int full_element_shifts = shift / ELEMENT_SIZE;
    unsigned int sub_shift = shift % ELEMENT_SIZE;

    /**
     *  Note that the shift may be more than the length of an unsigned int (ie over 32), this needs to be accounted for, so the element
     *  we're shifting from may be ahead a few elements in the array.  When we do the shift, we can do this quickly by getting the target bits
     *  shifted to the left and doing an or with a shift to the right.
     *  ie (if our elements had 8 bits):
     *      00011010 101111101
     *  doing a shift of 5, we could update the first one to:
     *     00010111         // src[i + (full_element_shifts = 0) + 1] >> ((ELEMENT_SIZE = 8) - (sub_shift = 5)) // shift right 3
     *     |
     *     01000000
     *  which would be:
     *     01010111
     *  then the next would just be the second element shifted to the left by 5:
     *     10100000
     *   which results in:
     *     01010111 10100000
     *   which is the whole array shifted to the left by 5
     */
    for (unsigned int i = 0; i < (length - full_element_shifts) - 1; i++) {
        dest[i] = src[i + full_element_shifts] << sub_shift | src[i + full_element_shifts + 1] >> (ELEMENT_SIZE - sub_shift);
    }
    dest[length - 1] = src[length - 1] << sub_shift;
}

/**
 *  updates dest to:
 *      dest != src
 *
 *  Where dest and src are two arrays with length elements
 */
static inline void or_equal(unsigned int *dest, const unsigned int length, const unsigned int *src) {
    for (unsigned int i = 0; i < length; i++) dest[i] |= src[i];
}

/**
 *  Adds the single bit (for a new set element) into dest:
 *
 *  dest |= 1 << number
 */
static inline void or_single(unsigned int *dest, const unsigned int length, const unsigned int number) {
    unsigned int pos = number / ELEMENT_SIZE;
    unsigned int tmp = number % ELEMENT_SIZE;

    dest[length - pos - 1] |= 1 << tmp;
}

/**
 *  Tests to see if all the bits are 1s between min and max
 */
static inline bool all_ones(const unsigned int *subset, const unsigned int length, const unsigned int min, const unsigned int max) {
    unsigned int min_pos = min / ELEMENT_SIZE;
    unsigned int min_tmp = min % ELEMENT_SIZE;
    unsigned int max_pos = max / ELEMENT_SIZE;
    unsigned int max_tmp = max % ELEMENT_SIZE;

    /*
     * This will print out all the 1s that we're looking for.

    if (min_pos < max_pos) {
        if (max_tmp > 0) {
            print_bits(UINT_MAX >> (ELEMENT_SIZE - max_tmp));
        } else {
            print_bits(0);
        }
        for (unsigned int i = min_pos + 1; i < max_pos - 1; i++) {
            print_bits(UINT_MAX);
        }
        print_bits(UINT_MAX << (min_tmp - 1));
    } else {
        print_bits((UINT_MAX << (min_tmp - 1)) & (UINT_MAX >> (ELEMENT_SIZE - max_tmp)));
    }
    */

    if (min_pos == max_pos) {
        unsigned int against = (UINT_MAX >> (ELEMENT_SIZE - max_tmp)) & (UINT_MAX << (min_tmp - 1));
        return against == (against & subset[length - max_pos - 1]);
    } else {
        unsigned int against = UINT_MAX << (min_tmp - 1);
        if (against != (against & subset[length - min_pos - 1])) {
            return false;
        }
//        fprintf(output_target, "min success\n");

        for (unsigned int i = (length - min_pos - 2); i > (length - max_pos); i--) {
            if (UINT_MAX != (UINT_MAX & subset[i])) {
                return false;
            }
        }
//        fprintf(output_target, "mid success\n");

        if (max_tmp > 0) {
            against = UINT_MAX >> (ELEMENT_SIZE - max_tmp);
            if (against != (against & subset[length - max_pos - 1])) {
                return false;
            }
        }
//        fprintf(output_target, "max success\n");

    }

    return true;
}

/**
 *  Tests to see if a subset all passes the subset sum hypothesis
 */
static inline bool test_subset(const unsigned int *subset, const unsigned int subset_size, const unsigned long long iteration, const unsigned int starting_subset, const bool doing_slice) {
    //this is also symmetric.  TODO: Only need to check from the largest element in the set (9) to the sum(S)/2 == (13), need to see if everything between 9 and 13 is a 1
    unsigned int M = subset[subset_size - 1];
    unsigned int max_subset_sum = 0;

    for (unsigned int i = 0; i < subset_size; i++) max_subset_sum += subset[i];
    
    for (unsigned int i = 0; i < max_sums_length; i++) {
        sums[i] = 0;
        new_sums[i] = 0;
    }

//    fprintf(output_target, "\n");
    unsigned int current;
    for (unsigned int i = 0; i < subset_size; i++) {
        current = subset[i];

        shift_left(new_sums, max_sums_length, sums, current);                    // new_sums = sums << current;
//        fprintf(output_target, "new_sums = sums << %2u    = ", current);
//        print_bit_array(new_sums, sums_length);
//        fprintf(output_target, "\n");

        or_equal(sums, max_sums_length, new_sums);                               //sums |= new_sums;
//        fprintf(output_target, "sums |= new_sums         = ");
//        print_bit_array(sums, sums_length);
//        fprintf(output_target, "\n");

        or_single(sums, max_sums_length, current - 1);                           //sums |= 1 << (current - 1);
//        fprintf(output_target, "sums != 1 << current - 1 = ");
//        print_bit_array(sums, sums_length);
//        fprintf(output_target, "\n");
    }

    bool success = all_ones(sums, max_sums_length, M, max_subset_sum - M);

#ifdef VERBOSE
#ifdef FALSE_ONLY
    if (!success) {
#endif

#ifdef SHOW_SUM_CALCULATION
    for (unsigned int i = 0; i < max_sums_length; i++) {
        sums[i] = 0;
        new_sums[i] = 0;
    }

    fprintf(output_target, "\n");
    for (unsigned int i = 0; i < subset_size; i++) {
        current = subset[i];

        shift_left(new_sums, max_sums_length, sums, current);                    // new_sums = sums << current;
        fprintf(output_target, "new_sums = sums << %2u                                          = ", current);
        print_bit_array(new_sums, max_sums_length);
        fprintf(output_target, "\n");

        or_equal(sums, max_sums_length, new_sums);                               //sums |= new_sums;
        fprintf(output_target, "sums |= new_sums                                               = ");
        print_bit_array(sums, max_sums_length);
        fprintf(output_target, "\n");

        or_single(sums, max_sums_length, current - 1);                           //sums |= 1 << (current - 1);
        fprintf(output_target, "sums != 1 << current - 1                                       = ");
        print_bit_array(sums, max_sums_length);
        fprintf(output_target, "\n");
    }
#endif

        if (doing_slice)    fprintf(output_target, "%15llu ", (iteration + starting_subset));
        else                fprintf(output_target, "%15llu ", iteration);
        print_subset(subset, subset_size);
        fprintf(output_target, " = ");

        unsigned int min = max_subset_sum - M;
        unsigned int max = M;
#ifdef ENABLE_COLOR
        print_bit_array_color(sums, max_sums_length, min, max);
#else 
        print_bit_array(sums, max_sums_length);
#endif

        fprintf(output_target, "  match %4u to %4u ", min, max);
        if (success)    fprintf(output_target, " = pass\n");
        else            fprintf(output_target, " = fail\n");

#ifdef FALSE_ONLY
    }
#endif
#endif

    return success;
}

/**
 *  calculates n!
 */
long double fac(unsigned int n) {
    long double result = 1;
    for (unsigned int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}

/**
 *  Calculate n choose k:
 *  TODO: implement this using long long ints instead of long double (there are precision issues to take into a ccount).
 */
static inline long double n_choose_k(unsigned int n, unsigned int k) {
    long double expected_total = fac(n) / (fac(k) * fac(n - k));
    return expected_total;
}

static inline unsigned long long n_choose_k_new(unsigned int n, unsigned int k) {
    unsigned int max, min;
    if (k < n - k) {
        max = n - k;
        min = k;
    } else {
        max = k;
        min = n - k;
    }

    unsigned int *divisors = new unsigned int[min - 1];
    for (unsigned int i = 2; i <= min; i++) divisors[i] = i;

    unsigned long long numerator = n;
    for (unsigned int j = n - 1; j > max; j++) {
        for (unsigned int k = 0; k < min - 1; k++) {
            if (numerator % divisors[k] == 0) {
                numerator /= divisors[k];
                divisors[k] = 1;
            }
        }
        n *= j;
    }

    delete [] divisors;

    return n;
}

static inline void generate_ith_subset(unsigned long long i, unsigned int *subset, unsigned int subset_size, unsigned int max_set_value) {
    unsigned int pos = 0;
    unsigned int current_value = 1;
    long double nck;

    while (pos < subset_size - 1) {
        nck = n_choose_k((max_set_value - 1) - current_value, (subset_size - 1) - (pos + 1));       //TODO: this does not need to be recalcualted, there is a faster way to do this

        if (i < nck || i == 1) { //it seems I also needed to check if i == 1 here, due to precision issues with long double (which could make the fac function in n_choose_k go into an infinite loop.
            subset[pos] = current_value;
            pos++;
        } else {
            i -= nck;
        }
        current_value++;
    }

    subset[subset_size - 1] = max_set_value;
}

static inline void generate_next_subset_td(unsigned int *subset, unsigned int subset_size, unsigned int max_set_value) {
    unsigned int current = subset_size - 2;
    subset[current]++;

//    fprintf(output_target, "subset_size: %u, max_set_value: %u\n", subset_size, max_set_value);

//    print_subset(subset, subset_size);
//    fprintf(output_target, "\n");

    while (current > 0 && subset[current] > (max_set_value - (subset_size - (current + 1)))) {
        subset[current - 1]++;
        current--;

//        print_subset(subset, subset_size);
//        fprintf(output_target, "\n");
    }

    while (current < subset_size - 2) {
        subset[current + 1] = subset[current] + 1;
        current++;

//        print_subset(subset, subset_size);
//        fprintf(output_target, "\n");
    }

    subset[subset_size - 1] = max_set_value;

//    print_subset(subset, subset_size);
//    fprintf(output_target, "\n");
}

/**
 * Jun: changes start
 * This algorithm keeps track of the bubbles between two adjacent elements in a subset.
 * Notion: N = subset_size; M = max_set_value;
 * The subset has (N+2) elements with subset[0] is fixed to be 1, subset[N+1] is fixed to be M, and subset[1] through subset[N] vary in the process.
 * There are (N+1) bubbles between pairs of adjacent elements.
 * Initially, the bubbles are initialized with bubbles[0] = M-N, and bubbles[1]=...=bubbles[N]=0.
 * Bubbles are gradually squeezed from left to right.
 * The process ends when bubbles[0]=...=bubbles[N-1]=0, and bubbles[N]=M-N.
 */
static inline void generate_next_subset_jl(unsigned int *subset, unsigned int subset_size, unsigned int max_set_value, unsigned int *bubbles) {
    unsigned int index = subset_size - 1;
    
    // Update the new sizes for the bubbles
    while (bubbles[index] == 0) {
        index--;
    }
    
    if (index < subset_size-1) {
        bubbles[index]--;
        bubbles[index+1]++;
        bubbles[subset_size-1] += bubbles[subset_size];
        bubbles[subset_size] = 0;
    } else { // this means that (index == subset_size-1)
        bubbles[index]--;
        bubbles[subset_size]++;
    }
    
    // write the subset under new bubbles
    subset[0] = 1;
    for (index = 1; index <= subset_size; index++) {
        subset[index] = subset[index-1] + bubbles[index];
    }
}

/*
void write_checkpoint(string filename, const unsigned long long iteration) {
#ifdef _BOINC_
    string output_path;
    int retval = boinc_resolve_filename_s(filename.c_str(), output_path);
    
    if (retval) {
        fprintf(stderr, "APP: error writing sites checkpoint (resolving checkpoint file name)\n");
        return;
    }   

    ofstream checkpoint_file(output_path.c_str());
#else
    ofstream checkpoint_file(filename.c_str());
#endif
    if (!checkpoint_file.is_open()) {
        fprintf(stderr, "APP: error writing checkpoint (opening checkpoint file)\n");
        return;
    }   

    checkpoint_file << "seed: " <<  seed << endl;
    checkpoint_file << "iteration: " << iteration << endl;
    checkpoint_file << "independent_walk: " << independent_walk << endl;
    write_sites_to_file(checkpoint_file, ".\n", sequences);

    checkpoint_file.close();
}
*/

int main(int argc, char** argv) {
#ifdef _BOINC_
    int retval = 0;
#ifdef BOINC_APP_GRAPHICS
#if defined(_WIN32) || defined(__APPLE)
    retval = boinc_init_graphics(worker);
#else
    retval = boinc_init_graphics(worker, argv[0]);
#endif
#else
    retval = boinc_init();
#endif

    if (retval) exit(retval);
#endif

#ifdef _BOINC_
    output_target = output_file;
#else 
    output_target = stdout;
#endif

    if (argc != 3 && argc != 5) {
        fprintf(stderr, "ERROR, wrong command line arguments.\n");
        fprintf(stderr, "USAGE:\n");
        fprintf(stderr, "\t./subset_sum <M> <N> [<i> <count>]\n\n");
        fprintf(stderr, "argumetns:\n");
        fprintf(stderr, "\t<M>      :   The maximum value allowed in the sets.\n");
        fprintf(stderr, "\t<N>      :   The number of elements allowed in a set.\n");
        fprintf(stderr, "\t<i>      :   (optional) start at the <i>th generated subset.\n");
        fprintf(stderr, "\t<count>  :   (optional) only test <count> subsets (starting at the <i>th subset).\n");
        exit(0);
    }

    unsigned long max_set_value = atol(argv[1]);
    unsigned long subset_size = atol(argv[2]);

#ifdef _BOINC_
    if (!started_from_checkpoint) {
#endif
        fprintf(output_target, "max_set_value: %lu, subset_size: %lu\n", max_set_value, subset_size);
        if (max_set_value < subset_size) {
            fprintf(stderr, "Error max_set_value < subset_size. Quitting.\n");
            exit(0);
        }
#ifdef _BOINC_
    }
#endif

    //timestamp flag
#ifdef TIMESTAMP
    time_t start_time;
    time( &start_time );
#ifdef _BOINC_
    if (!started_from_checkpoint) {
#endif
        fprintf(output_target, "start time: %s", ctime(&start_time) );
#ifdef _BOINC_
    }
#endif
#endif

    bool doing_slice = false;
    unsigned int starting_subset = 0;
    unsigned int subsets_to_calculate = 0;

    if (argc == 5) {
        doing_slice = true;
        starting_subset = atoi(argv[3]);
        subsets_to_calculate = atoi(argv[4]);
    }

    /**
     *  Calculate the maximum set length (in bits) so we can use this for printing out the values cleanly.
     */
    unsigned int *max_set = new unsigned int[subset_size];
    for (unsigned int i = 0; i < subset_size; i++) max_set[subset_size - i - 1] = max_set_value - i;
    for (unsigned int i = 0; i < subset_size; i++) max_sums_length += max_set[i];

//    sums_length /= 2;
    max_sums_length /= ELEMENT_SIZE;
    max_sums_length++;

    delete [] max_set;

    unsigned int *subset = new unsigned int[subset_size];
#ifdef NEXT_SUBSET_JUN_LIU
    unsigned int *bubbles = new unsigned int[subset_size + 1];
    bubbles[0] = max_set_value - subset_size;
    for (unsigned int i = 1; i <= subset_size; i++) bubbles[i] = 0;
#endif

//    this caused a problem:
//    
//    fprintf(output_target, "%15u ", 296010);
//    generate_ith_subset(296010, subset, subset_size, max_set_value);
//    print_subset(subset, subset_size);
//    fprintf(output_target, "\n");

/*
    for (unsigned long long i = 0; i < max; i++) {
        fprintf(output_target, "%15llu ", i);
        generate_ith_subset(i, subset, subset_size, max_set_value);
        print_subset(subset, subset_size);
        fprintf(output_target, "\n");
    }
*/

    long double expected_total = n_choose_k(max_set_value - 1, subset_size - 1);
    unsigned long long expected_total_u = n_choose_k(max_set_value - 1, subset_size - 1);

    if (doing_slice) {
        if (starting_subset >= expected_total) {
            fprintf(stderr, "starting subset [%u] > total subsets [%Lf]\n", starting_subset, expected_total);
            fprintf(stderr, "quitting.\n");
            exit(0);
        }
        generate_ith_subset(starting_subset, subset, subset_size, max_set_value);
    } else {
        for (unsigned int i = 0; i < subset_size - 1; i++) subset[i] = i + 1;
        subset[subset_size - 1] = max_set_value;
    }

    sums = new unsigned int[max_sums_length];
    new_sums = new unsigned int[max_sums_length];

    bool success;
    unsigned long long pass = 0;
    unsigned long long fail = 0;
    unsigned long long iteration = 0;

#ifdef _BOINC_
#ifdef _BOINC_
    if (!started_from_checkpoint) {
#endif
        fprintf(output_target, "<tested_subsets>\n");
#ifdef _BOINC_
    }
#endif
#endif

#ifndef NEXT_SUBSET_JUN_LIU
    while (subset[0] <= (max_set_value - subset_size + 1)) {
#else
    while (bubbles[0] > 0 || bubbles[subset_size] < (max_set_value - subset_size)) {
#endif
        success = test_subset(subset, subset_size, iteration, starting_subset, doing_slice);

        if (success)    pass++;
        else            fail++;

#ifndef NEXT_SUBSET_JUN_LIU
        generate_next_subset_td(subset, subset_size, max_set_value);
#else
        generate_next_subset_jl(subset, subset_size, max_set_value, bubbles);
#endif

        iteration++;
        if (doing_slice && iteration >= subsets_to_calculate) break;

#ifdef _BOINC_
        if (iteration % 10000) {
            double progress;
            if (doing_slice) {
                progress = (double)iteration / (double)subsets_to_calculate;
            } else {
                progress = (double)iteration / (double)max;
            }
            boinc_fraction_done(progress);
    //        printf("\r%lf", progress);

            if ((iteration % 10000000) == 0) {      //this works out to be a checkpoint every 10 seconds or so
    //                printf("\n*****Checkpointing: %d! *****\n", n_checkpoints);
                    boinc_checkpoint_completed();
            }
        }
#endif
    }

#ifdef _BOINC_
    fprintf(output_target, "</tested_subsets>\n");
    fprintf(output_target, "<extra_info>\n");
#endif

    /**
     * pass + fail should = M! / (N! * (M - N)!)
     */
    if (doing_slice) {
        fprintf(output_target, "expected to compute %u sets\n", subsets_to_calculate);
    } else {
        fprintf(output_target, "the expected total number of sets is: %Lf, (unsigned long long calculation: %llu)\n", expected_total, expected_total_u);
    }
    fprintf(output_target, "%llu total sets, %llu sets passed, %llu sets failed, %lf success rate.\n", pass + fail, pass, fail, ((double)pass / ((double)pass + (double)fail)));

#ifdef _BOINC_
    fprintf(output_target, "</extra_info>\n");
#endif

    delete [] subset;
    delete [] sums;
    delete [] new_sums;

#ifdef TIMESTAMP
    time_t end_time;
    time( &end_time );
    fprintf(output_target, "end time: %s", ctime(&end_time) );
    fprintf(output_target, "running time: %ld\n", end_time - start_time);
#endif

#ifdef _BOINC_
    boinc_finish(0);
#endif

    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR Args, int WinMode){
    LPSTR command_line;
    char* argv[100];
    int argc;

    command_line = GetCommandLine();
    argc = parse_command_line( command_line, argv );
    return main(argc, argv);
}
#endif

