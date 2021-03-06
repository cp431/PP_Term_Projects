#include "mpi.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Macro definition(s)
#ifndef SETBIT
#define SETBIT(A,k) ( A[(k/32)] |= (1 << (k%32)) )
#endif

// Constants
static const int ROOT = 0;

// Send/Recv Tags
static const int TAG_CHUNK_SIZE = 0;
static const int TAG_MATRIX_CHUNK_DATA = 1;
static const int TAG_BIT_MAP = 1;

int main(int argc, char *argv[])
{   
  int process_rank;
  int num_processors;

  double start_time = 0.0;
  double end_time = 0.0;
  
  // Initialize MPI
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &process_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &num_processors);

  // Check for command line input
  if (argc < 2)
  {
    if (process_rank == ROOT)
      printf("ERROR: Missing table size. Usage: ./main [table size]\n");
      
      // MPI clean-up
      MPI_Finalize();
      return 0;
  }
  
  start_time = MPI_Wtime();
  
  // Define problem paramaters
  long table_size = atol(argv[1]);
  const unsigned long long int num_values = table_size * table_size;
  const unsigned long long int cells = ((num_values - table_size) / 2) + table_size;

  // Calculate each processors chunk, the 
  // number of cells this processor will calculate
  unsigned long long chunk_sizes[num_processors];
  for (int i = 0; i < num_processors; ++i)
  {
      chunk_sizes[i] = floor((float) cells / num_processors);
    
      if (i < cells % num_processors)
        chunk_sizes[i] += 1;
  }

  // Define offset paramaters
  const int offset = 1;
  const int start = 1;

  // Initialize each processors chunk
  unsigned long long my_chunk = chunk_sizes[process_rank];
  unsigned long long end = 0LL;
  
  // Determine the ending position for each process
  for (int i = 0; i < process_rank; ++i)
     end += chunk_sizes[i];

  // "Fast-forward" each processor to their start location
  unsigned long long i = start;
  unsigned long long j = start;
  while (end > 0)
  {
      --end; --my_chunk; ++i;
      if (i == (table_size + offset)) 
      {
          ++j; i = j;
      }
  }

  // Reinitialize my_chunk after decrement
  my_chunk = chunk_sizes[process_rank];
  const unsigned int ints_in_bitarray = ceil(ceil(num_values / 8) / sizeof(unsigned int)) + 1;

  // Define a bitmap for each process
  unsigned int *unique_bit_map = (unsigned int*) calloc(ints_in_bitarray, sizeof(unsigned int));
  if (unique_bit_map == NULL)
      printf("FAILED TO CALLOC BITMAP\n");

  // Set the bit corrisponding to each product as a unique product
  unsigned long long int product = 0LL; 
  while (my_chunk > 0LL) 
  {
    product = i * j;
    
    if (product > num_values) {
      product = (i-1) * (j-1);
      my_chunk = 1LL;
    }
    
    SETBIT(unique_bit_map, product);
    ++i; --my_chunk;
    if (i == (table_size + offset)) 
    {
      ++j; i = j;
    }
  }
  
  const unsigned int half_size = (int) ceil((float)ints_in_bitarray / 2.0f);
  
  // Each process sends it's unique bitmap to the root process
  if (process_rank != ROOT) 
  { 
    MPI_Send(unique_bit_map, half_size, MPI_UNSIGNED, ROOT, TAG_BIT_MAP, MPI_COMM_WORLD);
    
    if (half_size > 1)
      MPI_Send(unique_bit_map + half_size, half_size, MPI_UNSIGNED, ROOT, TAG_BIT_MAP, MPI_COMM_WORLD);
    
    free(unique_bit_map);
  }
  
  if (process_rank == ROOT) 
  {
    // Allocate buffer space for each incoming bitmap
    unsigned int *incoming_bit_map = (unsigned int*) calloc(half_size, sizeof(unsigned int));
    
    for (int rank = 1; rank < num_processors; ++rank) 
    {
        // Get each unique bitmap from each process to compare against root bitmap
        MPI_Recv(incoming_bit_map, half_size, MPI_UNSIGNED, rank, TAG_BIT_MAP, MPI_COMM_WORLD, NULL);

        for (int i = 0; i < half_size; ++i)
          unique_bit_map[i] |= incoming_bit_map[i];
      
        if (half_size > 1)
        {
          MPI_Recv(incoming_bit_map, half_size, MPI_UNSIGNED, rank, TAG_BIT_MAP, MPI_COMM_WORLD, NULL);

          for (int i = 0; i < half_size; ++i)
              (unique_bit_map + half_size)[i] |= incoming_bit_map[i];
        }
    }
    
      // Free the incoming bitmap space
      free(incoming_bit_map);  
    
      static unsigned long long counter = 0LL;
    
      #pragma omp parallel for
      for (int i = 0; i < ints_in_bitarray; ++i)
      {
          #pragma omp critical
          counter += __builtin_popcount(unique_bit_map[i]);
      }

      // Print the total count
      printf("counter: %llu\n", counter);
      
      end_time = MPI_Wtime();
      printf("Wallclock time elapsed: %.2lf seconds\n", end_time - start_time);
    
      free(unique_bit_map);
  }
  
  // Finalize MPI
  MPI_Finalize();
  return 0;
}
